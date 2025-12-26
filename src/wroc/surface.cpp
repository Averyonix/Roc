#include "server.hpp"
#include "util.hpp"


const u32 wroc_wl_compositor_version = 6;
const u32 wroc_wl_subcompositor_version = 1;

static
void wroc_wl_compositor_create_region(wl_client* client, wl_resource* resource, u32 id)
{
    auto* new_resource = wl_resource_create(client, &wl_region_interface, wl_resource_get_version(resource), id);
    wroc_debug_track_resource(new_resource);
    auto* server = wroc_get_userdata<wroc_server>(resource);
    auto* region = wrei_create_unsafe<wroc_region>();
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
    auto* surface = wrei_create_unsafe<wroc_surface>();
    surface->server = server;
    surface->resource = new_resource;
    server->surfaces.emplace_back(surface);

    // Add surface to its own surface stack
    surface->pending.surface_stack.emplace_back(surface);
    surface->pending.committed |= wroc_surface_committed_state::surface_stack;

    wroc_resource_set_implementation_refcounted(new_resource, &wroc_wl_surface_impl, surface);

    // Enter primary display unconditionally
    wroc_output_enter_surface(server->output_layout->primary.get(), surface);
}

const struct wl_compositor_interface wroc_wl_compositor_impl = {
    .create_surface = wroc_wl_compositor_create_surface,
    .create_region  = wroc_wl_compositor_create_region,
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
    auto* region = wroc_get_userdata<wroc_region>(resource);
    region->region.add({{x, y}, {width, height}});
}

static
void wroc_wl_region_subtract(wl_client* client, wl_resource* resource, i32 x, i32 y, i32 width, i32 height)
{
    auto* region = wroc_get_userdata<wroc_region>(resource);

    region->region.subtract({{x, y}, {width, height}});
}

const struct wl_region_interface wroc_wl_region_impl = {
    .destroy  = wroc_simple_resource_destroy_callback,
    .add      = wroc_wl_region_add,
    .subtract = wroc_wl_region_subtract,
};

// -----------------------------------------------------------------------------

static
void wroc_wl_surface_attach(wl_client* client, wl_resource* resource, wl_resource* wl_buffer, i32 x, i32 y)
{
    auto* surface = wroc_get_userdata<wroc_surface>(resource);

    surface->pending.buffer = wl_buffer ? wroc_get_userdata<wroc_buffer>(wl_buffer) : nullptr;
    surface->pending.committed |= wroc_surface_committed_state::buffer;

    if (x || y) {
        if (wl_resource_get_version(resource) >= WL_SURFACE_OFFSET_SINCE_VERSION) {
            wl_resource_post_error(resource, WL_SURFACE_ERROR_INVALID_OFFSET,
                "Non-zero offset not allowed in wl_surface::attach since version %u", WL_SURFACE_OFFSET_SINCE_VERSION);
        } else {
            surface->pending.delta = { x, y };
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
void wroc_wl_surface_set_opaque_region(wl_client* client, wl_resource* resource, wl_resource* opaque_region)
{
    auto* surface = wroc_get_userdata<wroc_surface>(resource);
    auto* region = wroc_get_userdata<wroc_region>(opaque_region);
    if (region) {
        surface->pending.opaque_region = region->region;
    } else {
        surface->pending.opaque_region.clear();
    }
    surface->pending.committed |= wroc_surface_committed_state::opaque_region;
}

static
void wroc_wl_surface_set_input_region(wl_client* client, wl_resource* resource, wl_resource* input_region)
{
    auto* surface = wroc_get_userdata<wroc_surface>(resource);
    auto* region = wroc_get_userdata<wroc_region>(input_region);
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
    surface->pending.delta = { x, y };
    surface->pending.committed |= wroc_surface_committed_state::offset;
}

static
void wroc_surface_commit_state(wroc_surface* surface, wroc_surface_state& from, wroc_surface_state& to)
{
    // Update frame callbacks

    to.frame_callbacks.take_and_append_all(std::move(from.frame_callbacks));

    // Update buffer

    if (from.committed >= wroc_surface_committed_state::buffer) {

        if (&from == &surface->pending) {
            if (from.buffer && from.buffer->locked) {
                log_error("Client is attempting to commit a buffer that is already locked!");
            }
        }

        if (to.buffer) {
            to.buffer->unlock();
        }

        if (&from == &surface->pending) {
            if (from.buffer) {
                if (from.buffer->resource) {
                    to.buffer = from.buffer;
                    to.buffer->on_commit();
                } else {
                    log_warn("Pending buffer was destroyed, surface contents will be cleared");
                    to.buffer = nullptr;
                }
            } else if (to.buffer) {
                log_warn("Null buffer was attached, surface contents will be cleared");
                to.buffer = nullptr;
            }
        } else {
            to.buffer = std::move(from.buffer);
        }

        from.buffer = nullptr;
    }

    // Update opaque region

    if (from.committed >= wroc_surface_committed_state::opaque_region) {
        to.opaque_region = std::move(from.opaque_region);
    }

    // Update input region

    if (from.committed >= wroc_surface_committed_state::input_region) {
        to.input_region = std::move(from.input_region);
    }

    // Update offset delta

    if (from.committed >= wroc_surface_committed_state::offset) {
        to.delta = from.delta;
    } else {
        to.delta = {};
        to.committed -= wroc_surface_committed_state::offset;
    }

    // Update surface stack

    if (std::erase_if(from.surface_stack, [](auto& w) { return !w; })) {
        // Clean out tombstones
        from.committed |= wroc_surface_committed_state::surface_stack;
    }

    if (from.committed >= wroc_surface_committed_state::surface_stack) {
        to.surface_stack.clear();
        to.surface_stack.append_range(from.surface_stack);
    }

    // Update commit flags

    to.committed |= from.committed;
    from.committed = wroc_surface_committed_state::none;
}

static
void wroc_surface_commit(wroc_surface* surface, wroc_surface_commit_flags flags)
{
    bool synchronized = wroc_surface_is_synchronized(surface);
    bool apply = !synchronized || flags >= wroc_surface_commit_flags::from_parent;
    if (synchronized) {
        if (flags >= wroc_surface_commit_flags::from_parent) {
            wroc_surface_commit_state(surface, surface->cached, surface->current);
        } else {
            wroc_surface_commit_state(surface, surface->pending, surface->cached);
        }
    } else {
        if (surface->cached.committed > wroc_surface_committed_state::none) {
            // Apply remaining cached state
            wroc_surface_commit_state(surface, surface->pending, surface->cached);
            wroc_surface_commit_state(surface, surface->cached, surface->current);
        } else {
            wroc_surface_commit_state(surface, surface->pending, surface->current);
        }
    }

    // Buffer rects

    if (apply && surface->current.buffer) {
        surface->buffer_src = {{}, surface->current.buffer->extent};
        // TODO: inverse buffer_transform
        surface->buffer_dst.extent = vec2f64(surface->current.buffer->extent) / surface->current.buffer_scale;
    }

    // Commit addons

    for (auto& addon : surface->addons) {
        addon->on_commit(flags);
    }

    if (!apply) {
        return;
    }

    // Update subsurfaces

    for (auto& s : surface->current.surface_stack) {

        // Skip self
        if (s.get() == surface) continue;

        wroc_surface_commit(s.get(), wroc_surface_commit_flags::from_parent);
    }
}

static
void wroc_wl_surface_commit(wl_client* client, wl_resource* resource)
{
    auto* surface = wroc_get_userdata<wroc_surface>(resource);

    wroc_surface_commit(surface, {});
}

const struct wl_surface_interface wroc_wl_surface_impl = {
    .destroy              = wroc_simple_resource_destroy_callback,
    .attach               = wroc_wl_surface_attach,
    .damage               = WROC_STUB,
    .frame                = wroc_wl_surface_frame,
    .set_opaque_region    = wroc_wl_surface_set_opaque_region,
    .set_input_region     = wroc_wl_surface_set_input_region,
    .commit               = wroc_wl_surface_commit,
    .set_buffer_transform = WROC_STUB,
    .set_buffer_scale     = WROC_STUB,
    .damage_buffer        = WROC_STUB,
    .offset               = wroc_wl_surface_offset,
};

static
void wroc_surface_destroy_state(wroc_surface* surface, wroc_surface_state& state)
{
    // TODO: Should we send a done instead?
    while (auto* callback = state.frame_callbacks.front()) wl_resource_destroy(callback);
}

wroc_surface::~wroc_surface()
{
    std::erase(server->surfaces, this);

    wroc_surface_destroy_state(this, pending);
    wroc_surface_destroy_state(this, cached);
    wroc_surface_destroy_state(this, current);

    log_warn("wroc_surface DESTROY, this = {}", (void*)this);
}

bool wroc_surface_point_accepts_input(wroc_surface* surface, vec2f64 surface_pos)
{
    rect2f64 buffer_rect = {};
    if (surface->current.buffer) {
        buffer_rect.extent = vec2f64{surface->current.buffer->extent} / surface->current.buffer_scale;
    }

    // log_debug("buffer_rect = {}", wrei_to_string(buffer_rect));

    if (!wrei_rect_contains(buffer_rect, surface_pos)) return false;

    auto accepts_input = surface->current.input_region.contains(surface_pos);

    // log_trace("input_region.contains{} = {}", wrei_to_string(point), accepts_input);

    return accepts_input;
}

bool wroc_surface_is_synchronized(wroc_surface* surface)
{
    return surface->role_addon && surface->role_addon->is_synchronized();
}

void wroc_surface_raise(wroc_surface* surface)
{
    // TODO: Implement generic "slide" algorithm for this and subsurface layers

    auto i = std::ranges::find(surface->server->surfaces, surface);
    std::rotate(i, i + 1, surface->server->surfaces.end());
}

vec2f64 wroc_surface_get_position(wroc_surface* surface)
{
    switch (surface->role) {
        break;case wroc_surface_role::none:
            ;
        break;case wroc_surface_role::cursor:
              case wroc_surface_role::drag_icon:
            return surface->server->seat->pointer->position;
        break;case wroc_surface_role::subsurface:
            if (auto* subsurface = static_cast<wroc_subsurface*>(surface->role_addon.get())) {
                return wroc_surface_get_position(subsurface->parent.get()) + vec2f64(subsurface->current.position);
            }
        break;case wroc_surface_role::xdg_toplevel:
              case wroc_surface_role::xdg_popup:
            if (auto* xdg_surface = wroc_surface_get_addon<wroc_xdg_surface>(surface)) {
                auto geom = wroc_xdg_surface_get_geometry(xdg_surface);
                return xdg_surface->anchor.position
                    - vec2f64(geom.extent * xdg_surface->anchor.relative)
                    - vec2f64(geom.origin);
            }
    }

    log_warn("Surface has no valid position!");
    return {};
}

// -----------------------------------------------------------------------------

bool wroc_surface_put_addon(wroc_surface* surface, wroc_surface_addon* addon)
{
    auto role = addon->get_role();
    if (role != wroc_surface_role::none) {
        if (surface->role_addon) {
            log_error("Surface already has addon for role {}", magic_enum::enum_name(surface->role));
            return false;
        }

        if (surface->role == wroc_surface_role::none) {
            surface->role = role;
        } else if (surface->role != role) {
            log_error("Surface already has role {}, can't change to {}", magic_enum::enum_name(surface->role), magic_enum::enum_name(role));
            return false;
        }

        surface->role_addon = addon;
    }

    addon->surface = surface;
    surface->addons.emplace_back(addon);

    return true;
}

wroc_surface_addon* wroc_surface_get_addon(wroc_surface* surface, const std::type_info& type)
{
    if (!surface) return nullptr;
    auto iter = std::ranges::find_if(surface->addons, [&](const auto& a) { return typeid(*a.get()) == type; });
    return iter == surface->addons.end() ? nullptr : iter->get();
}

void wroc_surface_addon_detach(wroc_surface_addon* addon)
{
    if (!addon->surface) return;
    std::erase_if(addon->surface->addons, [&](const auto& a) { return a.get() == addon; });
    if (addon->surface->role_addon == addon) {
        addon->surface->role_addon = nullptr;
    }
    addon->surface = nullptr;
}

void wroc_surface_addon_destroy(wl_client*, wl_resource* resource)
{
    wroc_surface_addon_detach(wroc_get_userdata<wroc_surface_addon>(resource));
    wl_resource_destroy(resource);
}
