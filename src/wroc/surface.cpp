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
    pixman_region32_init(&region->region);
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
    pixman_region32_union_rect(&region->region, &region->region, x, y, width, height);
}

static
void wroc_wl_region_subtract(wl_client* client, wl_resource* resource, i32 x, i32 y, i32 width, i32 height)
{
    auto* region = wroc_get_userdata<wroc_wl_region>(resource);

    pixman_region32_union_rect(&region->region, &region->region, x, y, width, height);

    pixman_region32_t rect;
    pixman_region32_init_rect(&rect, x, y, width, height);
    pixman_region32_subtract(&region->region, &region->region, &rect);
    pixman_region32_fini(&rect);
}

const struct wl_region_interface wroc_wl_region_impl = {
    .add      = wroc_wl_region_add,
    .destroy  = wroc_simple_resource_destroy_callback,
    .subtract = wroc_wl_region_subtract,
};

wroc_wl_region::~wroc_wl_region()
{
    pixman_region32_fini(&region);
}

// -----------------------------------------------------------------------------

static
void wroc_wl_surface_attach(wl_client* client, wl_resource* resource, wl_resource* wl_buffer, i32 x, i32 y)
{
    auto* surface = wroc_get_userdata<wroc_surface>(resource);
    if (wl_buffer) {
        auto* buffer = wroc_get_userdata<wroc_shm_buffer>(wl_buffer);
        log_trace("Attaching buffer, type = {}", magic_enum::enum_name(buffer->type));
        surface->pending.buffer = buffer;
    } else {
        surface->pending.buffer = nullptr;
    }
    surface->pending.buffer_was_set = true;
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

    if (surface->role_addon) {
        surface->role_addon->on_commit();
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
}

const struct wl_surface_interface wroc_wl_surface_impl = {
    .destroy              = wroc_simple_resource_destroy_callback,
    .attach               = wroc_wl_surface_attach,
    .damage               = WROC_STUB,
    .frame                = wroc_wl_surface_frame,
    .set_opaque_region    = WROC_STUB,
    .set_input_region     = WROC_STUB,
    .commit               = wroc_wl_surface_commit,
    .set_buffer_transform = WROC_STUB,
    .set_buffer_scale     = WROC_STUB,
    .damage_buffer        = WROC_STUB,
    .offset               = WROC_STUB,
};

wroc_surface::~wroc_surface()
{
    std::erase(server->surfaces, this);
}
