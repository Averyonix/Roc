#include "server.hpp"
#include "util.hpp"

static
void wroc_wl_compositor_create_region(wl_client* client, wl_resource* resource, u32 id)
{
    auto* compositor = wroc_get_userdata<wroc_wl_compositor>(resource);
    auto* new_resource = wl_resource_create(client, &wl_region_interface, wl_resource_get_version(resource), id);
    wroc_debug_track_resource(new_resource);
    auto* region = new wroc_wl_region {};
    region->server = compositor->server;
    region->wl_region = new_resource;
    wl_resource_set_implementation(new_resource, &wroc_wl_region_impl, region, WROC_SIMPLE_RESOURCE_UNREF(wroc_wl_region, wl_region));
}

static
void wroc_wl_compositor_create_surface(wl_client* client, wl_resource* resource, u32 id)
{
    auto* compositor = wroc_get_userdata<wroc_wl_compositor>(resource);
    auto* new_resource = wl_resource_create(client, &wl_surface_interface, wl_resource_get_version(resource), id);
    wroc_debug_track_resource(new_resource);
    auto* surface = new wroc_surface {};
    surface->server = compositor->server;
    surface->wl_surface = new_resource;
    compositor->server->surfaces.emplace_back(surface);
    wl_resource_set_implementation(new_resource, &wroc_wl_surface_impl, surface, WROC_SIMPLE_RESOURCE_UNREF(wroc_surface, wl_surface));
}

const struct wl_compositor_interface wroc_wl_compositor_impl = {
    .create_region  = wroc_wl_compositor_create_region,
    .create_surface = wroc_wl_compositor_create_surface,
};

void wroc_wl_compositor_bind_global(wl_client* client, void* data, u32 version, u32 id)
{
    auto* new_resource = wl_resource_create(client, &wl_compositor_interface, version, id);
    wroc_debug_track_resource(new_resource);
    auto* compositor = new wroc_wl_compositor {};
    compositor->server = static_cast<wroc_server*>(data);
    compositor->wl_compositor = new_resource;
    wl_resource_set_implementation(new_resource, &wroc_wl_compositor_impl, compositor, WROC_SIMPLE_RESOURCE_UNREF(wroc_wl_compositor, wl_compositor));
};

// -----------------------------------------------------------------------------

static
void wroc_wl_region_add(wl_client* client, wl_resource* resource, i32 x, i32 y, i32 width, i32 height)
{
    auto* region = wroc_get_userdata<wroc_wl_region>(resource);
    region->region.add({{x, y}, {width, height}});
}

static
void wroc_wl_region_subtract(wl_client* client, wl_resource* resource, i32 x, i32 y, i32 width, i32 height)
{
    auto* region = wroc_get_userdata<wroc_wl_region>(resource);

    region->region.subtract({{x, y}, {width, height}});
}

const struct wl_region_interface wroc_wl_region_impl = {
    .add      = wroc_wl_region_add,
    .destroy  = wroc_simple_resource_destroy_callback,
    .subtract = wroc_wl_region_subtract,
};

// -----------------------------------------------------------------------------

static
void wroc_wl_surface_attach(wl_client* client, wl_resource* resource, wl_resource* wl_buffer, i32 x, i32 y)
{
    auto* surface = wroc_get_userdata<wroc_surface>(resource);
    if (wl_buffer) {
        auto* buffer = wroc_get_userdata<wroc_shm_buffer>(wl_buffer);
        // log_trace("Attaching buffer, type = {}", magic_enum::enum_name(buffer->type));
        surface->pending.buffer = buffer;
    } else {
        surface->pending.buffer = nullptr;
    }
    surface->pending.buffer_was_set = true;

    if (x || y) {
        if (wl_resource_get_version(resource) >= WL_SURFACE_OFFSET_SINCE_VERSION) {
            wl_resource_post_error(resource, WL_SURFACE_ERROR_INVALID_OFFSET,
                "Non-zero offset not allowed in wl_surface::attach since version %u", WL_SURFACE_OFFSET_SINCE_VERSION);
        } else {
            surface->pending.offset = { x, y };
        }
    }
}

static
void wroc_wl_surface_frame(wl_client* client, wl_resource* resource, u32 callback)
{
    auto* surface = wroc_get_userdata<wroc_surface>(resource);
    auto new_resource = wl_resource_create(client, &wl_callback_interface, 1, callback);
    wroc_debug_track_resource(new_resource);
    surface->pending.frame_callbacks.emplace_back(new_resource);
    wrei_add_ref(surface);
    wl_resource_set_implementation(new_resource, nullptr, surface, [](wl_resource* resource) {
        auto* surface = wroc_get_userdata<wroc_surface>(resource);
        // log_warn("frame callback {} destroyed", (void*)resource);
        std::erase(surface->pending.frame_callbacks, resource);
        std::erase(surface->current.frame_callbacks, resource);
        wrei_remove_ref(surface);
    });
}

static
void wroc_wl_surface_set_input_region(wl_client* client, wl_resource* resource, wl_resource* input_region)
{
    auto* surface = wroc_get_userdata<wroc_surface>(resource);
    auto* region = wroc_get_userdata<wroc_wl_region>(input_region);
    if (region) {
        surface->pending.input_region = region->region;
    } else {
        surface->pending.input_region = wrei_region({{0, 0}, {INT32_MAX, INT32_MAX}});
    }
}

static
void wroc_wl_surface_commit(wl_client* client, wl_resource* resource)
{
    auto* surface = wroc_get_userdata<wroc_surface>(resource);

    // Handle initial commit

    if (surface->initial_commit) {
        surface->initial_commit = false;

        if (surface->role_addon) {
            surface->role_addon->on_initial_commit();
        }
    }

    // Update frame callbacks

    surface->current.frame_callbacks.append_range(surface->pending.frame_callbacks);
    surface->pending.frame_callbacks.clear();

    // Update buffer

    if (surface->pending.buffer_was_set) {
        if (surface->pending.buffer && surface->pending.buffer->locked) {
            log_error("Client is attempting to commit buffer that is already locked!");
        }

        if (surface->current.buffer) {
            surface->current.buffer->unlock();
        }

        if (surface->pending.buffer) {
            if (surface->pending.buffer->wl_buffer) {
                surface->current.buffer = surface->pending.buffer;
                surface->current.buffer->on_commit();
            } else {
                log_warn("Pending buffer was destroyed, surface contents will be cleared");
                surface->current.buffer = nullptr;
            }
        } else if (surface->current.buffer) {
            log_warn("Null buffer was attached, surface contents will be cleared");
            surface->current.buffer = nullptr;
        }

        surface->pending.buffer = nullptr;
        surface->pending.buffer_was_set = false;
    }

    // Update input region

    if (surface->pending.input_region) {
        surface->current.input_region = std::move(*surface->pending.input_region);
        surface->pending.input_region = std::nullopt;
    }

    // Update offset

    if (surface->pending.offset) {
        // NOTE: This seems to be worded as if it's accumulative...
        //       > relative to the current buffer's upper left corner
        //       ...but wlroots treats it as if it's relative to the surface origin
        surface->current.offset = *surface->pending.offset;
        surface->pending.offset = {};
    }

    // Commit addons

    if (surface->role_addon) {
        surface->role_addon->on_commit();
    }
}

static
void wroc_wl_surface_offset(wl_client* client, wl_resource* resource, i32 x, i32 y)
{
    auto* surface = wroc_get_userdata<wroc_surface>(resource);
    surface->pending.offset = { x, y };
}

const struct wl_surface_interface wroc_wl_surface_impl = {
    .destroy              = wroc_simple_resource_destroy_callback,
    .attach               = wroc_wl_surface_attach,
    .damage               = WROC_STUB,
    .frame                = wroc_wl_surface_frame,
    .set_opaque_region    = WROC_STUB,
    .set_input_region     = wroc_wl_surface_set_input_region,
    .commit               = wroc_wl_surface_commit,
    .set_buffer_transform = WROC_STUB,
    .set_buffer_scale     = WROC_STUB,
    .damage_buffer        = WROC_STUB,
    .offset               = wroc_wl_surface_offset,
};

wroc_surface::~wroc_surface()
{
    std::erase(server->surfaces, this);

    log_warn("wroc_surface DESTROY, this = {}", (void*)this);
}

bool wroc_surface_point_accepts_input(wroc_surface* surface, wrei_vec2f64 point)
{
    wrei_rect<f64> buffer_rect = {};
    buffer_rect.origin = surface->current.offset;
    if (surface->current.buffer) {
        buffer_rect.extent = wrei_vec2f64{surface->current.buffer->extent} / surface->current.buffer_scale;
    }

    // log_debug("buffer_rect = (({}, {}), ({}, {}))", buffer_rect.origin.x, buffer_rect.origin.y, buffer_rect.extent.x, buffer_rect.extent.y);

    if (!buffer_rect.contains(point)) return false;
    return surface->current.input_region.contains(point);
}
