#include "backend.hpp"
#include <wroc/event.hpp>

struct drm_resources
{
    std::vector<drmModeConnector*> connectors;
    std::vector<drmModeEncoder*> encoders;
    std::vector<drmModeCrtc*> crtcs;
    std::vector<drmModePlane*> planes;

    drm_resources(int drm_fd)
    {
        auto mode_res = wrei_unix_check_null(drmModeGetResources(drm_fd));
        if (!mode_res) {
            log_warn("Failed to get mode resources");
            return;
        }
        defer { drmModeFreeResources(mode_res); };

        auto plane_res = wrei_unix_check_null(drmModeGetPlaneResources(drm_fd));
        if (!plane_res) {
            log_warn("Failed to get plane resources");
            return;
        }
        defer { drmModeFreePlaneResources(plane_res); };

#define DO(Type, Name, Res) \
        for (decltype(Res->count_ ## Name) i = 0; i < Res->count_ ## Name; ++i) { \
            Name.emplace_back(drmModeGet ## Type(drm_fd, Res->Name[i])); \
        }

        DO(Connector, connectors, mode_res)
        DO(Encoder, encoders, mode_res)
        DO(Crtc, crtcs, mode_res)
        DO(Plane, planes, plane_res)

#undef DO
    }

#define DO(Type, Name) \
    drmMode##Type* find_##Name(u32 id) \
    { \
        for (auto* c : Name##s) if (c->Name##_id == id) return c;\
        return nullptr; \
    }

    DO(Connector, connector)
    DO(Encoder, encoder)
    DO(Crtc, crtc)
    DO(Plane, plane)

#undef DO

    ~drm_resources()
    {
        for (auto* c : connectors) drmModeFreeConnector(c);
        for (auto* e : encoders) drmModeFreeEncoder(e);
        for (auto* c : crtcs) drmModeFreeCrtc(c);
        for (auto* p : planes) drmModeFreePlane(p);
    }
};

struct drm_property_map
{
    ankerl::unordered_dense::map<std::string_view, drmModePropertyRes*> properties;

    drm_property_map() = default;

    drm_property_map(wroc_direct_backend* backend, u32 object_id, u32 object_type)
    {
        auto* props = drmModeObjectGetProperties(backend->drm_fd, object_id, object_type);
        defer { drmModeFreeObjectProperties(props); };

        for (u32 i = 0; i < props->count_props; ++i) {
            auto* prop = drmModeGetProperty(backend->drm_fd, props->props[i]);
            properties[prop->name] = prop;
        }
    }

    drm_property_map(const drm_property_map& other)
    {
        for (auto[name, old_prop] : other.properties) {
            auto* new_prop =  drmModeGetProperty(wroc_get_direct_backend()->drm_fd, old_prop->prop_id);
            properties[new_prop->name] = new_prop;
        }
    }

    drm_property_map& operator=(const drm_property_map& other)
    {
        if (this != &other) {
            this->~drm_property_map();
            new(this) drm_property_map(other);
        }
        return *this;
    };

    drm_property_map(drm_property_map&& other)
        : properties(std::move(other.properties))
    {}

    drm_property_map& operator=(drm_property_map&& other)
    {
        if (this != &other) {
            this->~drm_property_map();
            new(this) drm_property_map(std::move(other));
        }
        return *this;
    }

    ~drm_property_map()
    {
        for (auto[_, prop] : properties) drmModeFreeProperty(prop);
    }

    u32 get_prop_id(std::string_view prop_name)
    {
        return properties.at(prop_name)->prop_id;
    }

    int get_enum_value(std::string_view prop_name, std::string_view enum_name)
    {
        auto* prop = properties.at(prop_name);

        wrei_assert(prop->flags & DRM_MODE_PROP_ENUM);
        for (int e = 0; e < prop->count_enums; ++e) {
            if (enum_name == prop->enums[e].name) {
                return prop->enums[e].value;
            }
        }

        log_error("Failed to find enum value: {}.{}", prop_name, enum_name);
        wrei_debugkill();
    }
};

// -----------------------------------------------------------------------------

struct wroc_drm_buffer
{
    ref<wren_image> image;
    u32 fb2_id;
    bool free = true;
};

struct page_flip_event
{
    wroc_direct_backend* backend;
    wroc_drm_output* output;
    wren_image* image;
    int in_fence;
};

struct wroc_drm_output_state
{
    u32 primary_plane_id;
    u32 crtc_id;
    u32 connector_id;
    u64 refresh_mHz;

    drm_property_map plane_prop;
    drm_property_map crtc_prop;

    std::vector<wroc_drm_buffer> drm_buffers;
    wren_image* frontbuffer = nullptr;

    std::deque<page_flip_event> page_flips;
};

// -----------------------------------------------------------------------------

wroc_drm_output::~wroc_drm_output()
{
    for (auto& buffer : state->drm_buffers) {
        drmCloseBufferHandle(wroc_get_direct_backend()->drm_fd, buffer.fb2_id);
    }

    delete state;
}

static
void add_output(wroc_direct_backend* backend, drm_resources* resources, drmModeConnector* connector)
{
    // TODO: In the interest of fast prototyping, we will initially just re-use the existing
    //       configuration that we find, which should be that of the current TTY that we are
    //       being launched from.
    //
    //       In the future, we will want to handle reconfiguration of all DRM resources.

    // Find encoder for this connector

    if (connector->encoder_id == 0) {
        log_warn("Connector {} has no encoder", connector->connector_id);
        return;
    }
    auto encoder = resources->find_encoder(connector->encoder_id);
    wrei_assert(encoder);

    // Find CRTC currently used by this connector

    if (encoder->crtc_id == 0) {
        log_warn("Connector {} has no active CRTC", connector->connector_id);
        return;
    }
    auto* crtc = resources->find_crtc(encoder->crtc_id);
    wrei_assert(crtc);

    // Ensure the CRTC is active

    if (crtc->buffer_id == 0) {
        log_warn("Connector {} not active", connector->connector_id);
        return;
    }

    // Find plane currently used by CRTC

    drmModePlane* plane = nullptr;
    for (auto[i, p] : resources->planes | std::views::enumerate) {
        if (p->crtc_id == crtc->crtc_id && p->fb_id == crtc->buffer_id) {
            plane = p;
        }
    }
    wrei_assert(plane);

    // Compute refresh rate

    u64 refresh = ((crtc->mode.clock * 1000000ul / crtc->mode.htotal) + (crtc->mode.vtotal / 2)) / crtc->mode.vtotal;

    log_warn("Creating output");
    log_warn("  crtc: {}", crtc->crtc_id);
    log_warn("  conn: {}", connector->connector_id);
    log_warn("  plane: {}", plane->plane_id);
    log_warn("  refresh: {} mHz", refresh);

    auto output = wrei_create<wroc_drm_output>();
    output->state = new wroc_drm_output_state {
        .primary_plane_id = plane->plane_id,
        .crtc_id = crtc->crtc_id,
        .connector_id = connector->connector_id,
        .refresh_mHz = refresh,

        .plane_prop = { backend, plane->plane_id, DRM_MODE_OBJECT_PLANE },
        .crtc_prop = { backend, crtc->crtc_id, DRM_MODE_OBJECT_CRTC },
    };

    output->size = { 3840, 2160 };

    output->desc.modes = {
        {
            .size = output->size,
            .refresh = 0,
        }
    };

    output->desc.physical_size_mm = {};
    output->desc.model = "Unknown";
    output->desc.make = "Unknown";
    output->desc.name = std::format("DRM-{}", (void*)output.get());
    output->desc.description = std::format("DRM output {}", (void*)output.get());

    backend->outputs.emplace_back(output);
}

void wroc_backend_start_drm(wroc_direct_backend* backend)
{
    for (auto& output : backend->outputs) {
        wroc_post_event(wroc_output_event {
            .type = wroc_event_type::output_added,
            .output = output.get(),
        });
        wroc_post_event(wroc_output_event {
            .type = wroc_event_type::output_frame_requested,
            .output = output.get(),
        });
    }
}

static
void on_page_flip(int fd, u32 sequence, u32 tv_sec, u32 tv_usec, u32 crtc_id, void* data);

static
void drm_handle_event(wroc_direct_backend* backend, int fd, u32 mask)
{
    drmEventContext handlers {
        .version = 3,
        .page_flip_handler2 = on_page_flip,
    };
    drmHandleEvent(fd, &handlers);
}

void wroc_backend_init_drm(wroc_direct_backend* backend)
{
    int drm_fd = -1;
    {
        auto num_devices = drmGetDevices2(0, nullptr, 0);
        std::vector<drmDevice*> devices(num_devices);
        num_devices = drmGetDevices2(0, devices.data(), devices.size());
        devices.resize(std::min(devices.size(), usz(num_devices)));
        defer { drmFreeDevices(devices.data(), devices.size()); };

        for (auto candidate : devices) {
            if (!(candidate->available_nodes & (1 << DRM_NODE_PRIMARY))) continue;

            auto device = wroc_open_restricted(backend, candidate->nodes[DRM_NODE_PRIMARY]);
            if (!device) continue;

            drm_fd = device->fd;
        }
    }
    if (drm_fd < 0) {
        log_error("Failed to find suitable DRM node for direct backend!");
        wrei_debugkill();
    }

    backend->drm_fd = drm_fd;

    backend->drm_event_source = wrei_event_loop_add_fd(server->event_loop.get(), drm_fd, EPOLLIN,
        [backend](int fd, u32 events) {
            drm_handle_event(backend, fd, events);
        });

    drm_magic_t magic;
    log_debug("Getting magic");
    wrei_unix_check_n1(drmGetMagic(drm_fd, &magic));
    log_debug("Authenticating magic");
    wrei_unix_check_n1(drmAuthMagic(drm_fd, magic));

    log_debug("Setting universal planes capability");
    wrei_unix_check_n1(drmSetClientCap(drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1));
    log_debug("Setting atomic capability");
    wrei_unix_check_n1(drmSetClientCap(drm_fd, DRM_CLIENT_CAP_ATOMIC, 1));

    u64 cap = 0;

    log_debug("Checking for framebuffer modifier support");
    wrei_assert(wrei_unix_check_n1(drmGetCap(drm_fd, DRM_CAP_ADDFB2_MODIFIERS, &cap)) == 0 && cap);

    log_debug("Checking for monotonic timestamp support");
    wrei_assert(wrei_unix_check_n1(drmGetCap(drm_fd, DRM_CAP_TIMESTAMP_MONOTONIC, &cap)) == 0 && cap);

    drm_resources res(drm_fd);
    for (auto* connector : res.connectors) {
        add_output(backend, &res, connector);
    }
}

// -----------------------------------------------------------------------------

static
void on_page_flip(int fd, u32 sequence, u32 tv_sec, u32 tv_usec, u32 crtc_id, void* data)
{
    // u64 ms = tv_sec * 1000 + tv_usec / 1000;
    // log_trace("DRM [{}:{}] page flip, crtc = {}", sequence, ms, crtc_id);

    auto* event = static_cast<page_flip_event*>(data);

    if (event->output->state->frontbuffer) {
        for (auto[i, buffer] : event->output->state->drm_buffers | std::views::enumerate) {
            if (buffer.image.get() == event->output->state->frontbuffer) {
                buffer.free = true;
                break;
            }
        }
    }
    event->output->state->frontbuffer = event->image;

    close(event->in_fence);

    wroc_post_event(wroc_output_event {
        .type = wroc_event_type::output_frame_requested,
        .output = event->output,
    });

    // Page flips should always occur in FIFO order, so the page flip for this
    // event should always be at the front of the queue.
    wrei_assert(&event->output->state->page_flips.front() == event);
    event->output->state->page_flips.pop_front();
}

wren_image* wroc_drm_output::acquire()
{
    for (auto& buffer : state->drm_buffers) {
        if (buffer.free) {
            buffer.free = false;
            return buffer.image.get();
        }
    }

    log_error("ALLOCATING NEW FRAME BUFFER IMAGE");

    auto* wren = server->renderer->wren.get();
    auto drm_fd = wroc_get_direct_backend()->drm_fd;

    auto format = wren_format_from_drm(DRM_FORMAT_ARGB8888);
    auto usage = wren_image_usage::render;
    auto* format_props = wren_get_format_props(wren, format, usage);
    auto image = wren_image_create_dmabuf(wren, size, format, usage, format_props->mods);

    auto dma_params = wren_image_export_dmabuf(image.get());

    // Acquire GEM handles and prepare for import

    u32 handles[4] = {};
    u32 pitches[4] = {};
    u32 offsets[4] = {};
    u64 modifiers[4] = {};
    for (u32 i = 0; i < dma_params.planes.count; ++i) {
        wrei_unix_check_n1(drmPrimeFDToHandle(drm_fd, dma_params.planes[i].fd.get(), &handles[i]));
        log_warn("  plane[{}] prime fd {} -> GEM handle {}", i, dma_params.planes[i].fd.get(), handles[i]);
        pitches[i] = dma_params.planes[i].stride;
        offsets[i] = dma_params.planes[i].offset;
        modifiers[i] = dma_params.modifier;
    }

    // Import

    u32 buf_id = 0;
    wrei_unix_check_n1(drmModeAddFB2WithModifiers(drm_fd,
        size.x, size.y,
        format->drm, handles, pitches, offsets, modifiers,
        &buf_id, DRM_MODE_FB_MODIFIERS));

    // Close GEM handles

    std::flat_set<u32> unique_handles;
    unique_handles.insert_range(handles);
    for (auto handle : unique_handles) drmCloseBufferHandle(drm_fd, handle);

    log_warn("  FB2 id: {}", buf_id);

    auto& buffer = state->drm_buffers.emplace_back();
    buffer.image = image;
    buffer.fb2_id = buf_id;
    buffer.free = false;

    return buffer.image.get();
}

void wroc_drm_output::present(wren_image* image, wren_syncpoint wait)
{
    auto backend = wroc_get_direct_backend();

    wroc_drm_buffer* buffer = nullptr;
    for (auto& b : state->drm_buffers) {
        if (b.image.get() == image) {
            buffer = &b;
            break;
        }
    }
    wrei_assert(buffer);

    auto req = drmModeAtomicAlloc();

    auto plane_set = [&](std::string_view name, u64 value) {
        drmModeAtomicAddProperty(req, state->primary_plane_id, state->plane_prop.get_prop_id(name), value);
    };

    auto in_fence = wren_semaphore_export_syncfile(wait.semaphore, wait.value);

    plane_set("FB_ID", buffer->fb2_id);
    plane_set("IN_FENCE_FD", in_fence);
    plane_set("SRC_X", 0);
    plane_set("SRC_Y", 0);
    plane_set("SRC_W", image->extent.x << 16);
    plane_set("SRC_H", image->extent.y << 16);
    plane_set("CRTC_X", 0);
    plane_set("CRTC_Y", 0);
    plane_set("CRTC_W", size.x);
    plane_set("CRTC_H", size.y);

    auto* page_flip = &state->page_flips.emplace_back(page_flip_event {
        .backend = backend,
        .output = this,
        .image = image,
        .in_fence = in_fence,
    });
    wrei_unix_check_n1(drmModeAtomicCommit(backend->drm_fd, req, DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT, page_flip));

    drmModeAtomicFree(req);
}
