#include "server.hpp"
#include "util.hpp"

static
void wroc_wl_compositor_create_region(wl_client* client, wl_resource* resource, u32 id)
{
    auto* new_resource = wl_resource_create(client, &wl_region_interface, wl_resource_get_version(resource), id);
    wroc_debug_track_resource(new_resource);
    auto* server = wroc_get_userdata<wroc_server>(resource);
    auto* region = wrei_get_registry(server)->create<wroc_wl_region>();
    region->server = server;
    region->resource = new_resource;
    wroc_resource_set_implementation_refcounted(new_resource, &wroc_wl_region_impl, region);
}

static
void wroc_wl_compositor_create_surface(wl_client* client, wl_resource* resource, u32 id)
{
    auto* server = wroc_get_userdata<wroc_server>(resource);
    auto* new_resource = wl_resource_create(client, &wl_surface_interface, wl_resource_get_version(resource), id);
    wroc_debug_track_resource(new_resource);
    auto* surface = wrei_get_registry(server)->create<wroc_surface>();
    surface->server = server;
    surface->resource = new_resource;
    server->surfaces.emplace_back(surface);

    // Add surface to its own surface stack
    surface->pending.surface_stack.emplace_back(surface);
    surface->pending.committed |= wroc_surface_committed_state::surface_stack;

    wroc_resource_set_implementation_refcounted(new_resource, &wroc_wl_surface_impl, surface);
}

const struct wl_compositor_interface wroc_wl_compositor_impl = {
    .create_region  = wroc_wl_compositor_create_region,
    .create_surface = wroc_wl_compositor_create_surface,
};

void wroc_wl_compositor_bind_global(wl_client* client, void* data, u32 version, u32 id)
{
    auto* new_resource = wl_resource_create(client, &wl_compositor_interface, version, id);
    wroc_debug_track_resource(new_resource);
    wroc_resource_set_implementation(new_resource, &wroc_wl_compositor_impl, static_cast<wroc_server*>(data));
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

    surface->pending.buffer = wl_buffer ? wroc_get_userdata<wroc_wl_buffer>(wl_buffer) : nullptr;
    surface->pending.committed |= wroc_surface_committed_state::buffer;

    if (x || y) {
        if (wl_resource_get_version(resource) >= WL_SURFACE_OFFSET_SINCE_VERSION) {
            wl_resource_post_error(resource, WL_SURFACE_ERROR_INVALID_OFFSET,
                "Non-zero offset not allowed in wl_surface::attach since version %u", WL_SURFACE_OFFSET_SINCE_VERSION);
        } else {
            surface->pending.offset = { x, y };
            surface->pending.committed |= wroc_surface_committed_state::offset;
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
    wroc_resource_set_implementation(new_resource, nullptr, surface);
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
    surface->pending.committed |= wroc_surface_committed_state::input_region;
}

static
void wroc_wl_surface_offset(wl_client* client, wl_resource* resource, i32 x, i32 y)
{
    auto* surface = wroc_get_userdata<wroc_surface>(resource);
    surface->pending.offset = { x, y };
    surface->pending.committed |= wroc_surface_committed_state::offset;
}

void wroc_surface_commit(wroc_surface* surface)
{
    // Update frame callbacks

    surface->current.frame_callbacks.take_and_append_all(std::move(surface->pending.frame_callbacks));

    // Update buffer

    if (surface->pending.committed >= wroc_surface_committed_state::buffer) {
        if (surface->pending.buffer && surface->pending.buffer->locked) {
            log_error("Client is attempting to commit buffer that is already locked!");
        }

        if (surface->current.buffer) {
            surface->current.buffer->unlock();
        }

        if (surface->pending.buffer) {
            if (surface->pending.buffer->resource) {
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
    }

    // Update input region

    if (surface->pending.committed >= wroc_surface_committed_state::input_region) {
        surface->current.input_region = std::move(surface->pending.input_region);
    }

    // Update offset

    if (surface->pending.committed >= wroc_surface_committed_state::offset) {
        // NOTE: This seems to be worded as if it's accumulative...
        //       > relative to the current buffer's upper left corner
        //       ...but wlroots treats it as if it's relative to the surface origin
        surface->current.offset = surface->pending.offset;
    }

    // Update surface stack

    if (std::erase_if(surface->pending.surface_stack, [](auto& w) { return !w; })) {
        // Clean out tombstones
        surface->pending.committed |= wroc_surface_committed_state::surface_stack;
    }

    if (surface->pending.committed >= wroc_surface_committed_state::surface_stack) {
        surface->current.surface_stack.clear();
        surface->current.surface_stack.append_range(surface->pending.surface_stack);
    }

    surface->current.committed |= surface->pending.committed;
    surface->pending.committed = wroc_surface_committed_state::none;

    // Commit addons

    if (surface->role_addon) {
        surface->role_addon->on_commit();
    }

    // Set output

    if (surface->current.buffer) {
        if (!surface->output) {
            // TODO: Proper selection of output to bind to
            for (auto* output : surface->server->outputs) {
                wroc_surface_set_output(surface, output);
                break;
            }
        }
    } else if (surface->output) {
        wroc_surface_set_output(surface, nullptr);
    }

    // Update subsurfaces

    for (auto& s : surface->current.surface_stack) {

        // Skip self
        if (s.get() == surface) continue;

        if (auto* subsurface = wroc_subsurface::try_from(s.get())) {
            subsurface->on_parent_commit();
        }
    }
}

static
void wroc_wl_surface_commit(wl_client* client, wl_resource* resource)
{
    auto* surface = wroc_get_userdata<wroc_surface>(resource);

    if (surface->role_addon && surface->role_addon->is_synchronized()) {
        return;
    }

    wroc_surface_commit(surface);
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

    // TODO: Should we send a done instead?
    while (auto* callback = pending.frame_callbacks.front()) {
        wl_resource_destroy(callback);
    }
    while (auto* callback = current.frame_callbacks.front()) {
        wl_resource_destroy(callback);
    }

    log_warn("wroc_surface DESTROY, this = {}", (void*)this);
}

bool wroc_surface_point_accepts_input(wroc_surface* surface, vec2f64 point)
{
    rect2f64 buffer_rect = {};
    buffer_rect.origin = surface->current.offset;
    if (surface->current.buffer) {
        buffer_rect.extent = vec2f64{surface->current.buffer->extent} / surface->current.buffer_scale;
    }

    // log_debug("buffer_rect = (({}, {}), ({}, {}))", buffer_rect.origin.x, buffer_rect.origin.y, buffer_rect.extent.x, buffer_rect.extent.y);

    if (!wrei_rect_contains(buffer_rect, point)) return false;

    auto accepts_input = surface->current.input_region.contains(point);

    // log_trace("input_region.contains({}, {}) = {}", point.x, point.y, accepts_input);

    return accepts_input;
}
