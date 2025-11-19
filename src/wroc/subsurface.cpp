#include "server.hpp"
#include "util.hpp"

static
void wroc_wl_subcompositor_get_subsurface(wl_client* client, wl_resource* resource, u32 id, wl_resource* surface, wl_resource* parent)
{
    auto* new_resource = wl_resource_create(client, &wl_subsurface_interface, wl_resource_get_version(resource), id);
    wroc_debug_track_resource(new_resource);
    auto* subsurface = new wroc_subsurface {};
    subsurface->surface = wroc_get_userdata<wroc_surface>(surface);
    subsurface->surface->role_addon = subsurface;
    subsurface->resource = new_resource;
    subsurface->parent = wroc_get_userdata<wroc_surface>(parent);

    // Add subsurface to top of its parent's surface stack
    subsurface->parent->pending.surface_stack.emplace_back(subsurface->surface.get());
    subsurface->parent->pending.committed |= wroc_surface_committed_state::surface_stack;

    wroc_resource_set_implementation_refcounted(new_resource, &wroc_wl_subsurface_impl, subsurface);
}

const struct wl_subcompositor_interface wroc_wl_subcompositor_impl = {
    .destroy        = wroc_simple_resource_destroy_callback,
    .get_subsurface = wroc_wl_subcompositor_get_subsurface,
};

void wroc_wl_subcompositor_bind_global(wl_client* client, void* data, u32 version, u32 id)
{
    auto* new_resource = wl_resource_create(client, &wl_subcompositor_interface, version, id);
    wroc_debug_track_resource(new_resource);
    wroc_resource_set_implementation(new_resource, &wroc_wl_subcompositor_impl, static_cast<wroc_server*>(data));
}

// -----------------------------------------------------------------------------

static
void wroc_wl_subsurface_set_position(wl_client* client, wl_resource* resource, i32 x, i32 y)
{
    auto* subsurface = wroc_get_userdata<wroc_subsurface>(resource);
    subsurface->pending.position = {x, y};
    subsurface->pending.committed |= wroc_subsurface_committed_state::position;
}

static
void wroc_wl_subsurface_place(wl_resource* resource, wl_resource* _sibling, bool above)
{
    auto subsurface = wroc_get_userdata<wroc_subsurface>(resource);
    auto sibling = wroc_get_userdata<wroc_surface>(_sibling);

    auto& surface_stack = subsurface->parent->pending.surface_stack;

    if (sibling == subsurface->parent.get()) {
        wl_resource_post_error(subsurface->resource, WL_SUBSURFACE_ERROR_BAD_SURFACE, "sibling must not be own surface");
        return;
    }

    auto debug_print = [&]{
        auto i = std::ranges::find_if(surface_stack, [&](auto& w) { return w.get() == sibling; });
        auto c = std::ranges::find_if(surface_stack, [&](auto& w) { return w.get() == subsurface->surface.get(); });
        log_debug("  size = {}, self = {}, sibling = {}", surface_stack.size(), std::distance(surface_stack.begin(), c), std::distance(surface_stack.begin(), i));
    };

    log_debug("SUBSURFACE PLACE {}", above ? "ABOVE" : "BELOW");
    debug_print();

    if (std::ranges::find_if(surface_stack, [&](auto& w) { return w.get() == sibling; }) == surface_stack.end()) {
        wl_resource_post_error(subsurface->resource, WL_SUBSURFACE_ERROR_BAD_SURFACE, "sibling is not a sibling surface");
        return;
    }

    std::erase_if(surface_stack, [&](auto& w) { return w.get() == subsurface->surface.get(); });

    auto i = std::ranges::find_if(surface_stack, [&](auto& w) { return w.get() == sibling; });
    if (above) i = std::next(i);

    subsurface->parent->pending.surface_stack.insert(i, subsurface->surface.get());
    subsurface->parent->pending.committed |= wroc_surface_committed_state::surface_stack;

    debug_print();
}

static
void wroc_wl_subsurface_place_above(wl_client* client, wl_resource* resource, wl_resource* sibling)
{
    wroc_wl_subsurface_place(resource, sibling, true);
}

static
void wroc_wl_subsurface_place_below(wl_client* client, wl_resource* resource, wl_resource* sibling)
{
    wroc_wl_subsurface_place(resource, sibling, false);
}

static
void wroc_wl_subsurface_set_sync(wl_client* client, wl_resource* resource)
{
    log_warn("Subsurface mode set to: synchronized");
    wroc_get_userdata<wroc_subsurface>(resource)->synchronized = true;
}

static
void wroc_wl_subsurface_set_desync(wl_client* client, wl_resource* resource)
{
    log_warn("Subsurface mode set to: desynchronized");
    wroc_get_userdata<wroc_subsurface>(resource)->synchronized = false;
}

void wroc_subsurface::on_parent_commit()
{
    if (is_synchronized()) {
        wroc_surface_commit(surface.get());
    }
}

void wroc_subsurface::on_commit()
{
    if (pending.committed >= wroc_subsurface_committed_state::position) {
        current.position = pending.position;
    }

    current.committed |= pending.committed;
    pending = {};
}

bool wroc_subsurface::is_synchronized()
{
    if (synchronized) return true;
    return parent && parent->role_addon && parent->role_addon->is_synchronized();
}

const struct wl_subsurface_interface wroc_wl_subsurface_impl = {
    .destroy      = wroc_simple_resource_destroy_callback,
    .set_position = wroc_wl_subsurface_set_position,
    .place_above  = wroc_wl_subsurface_place_above,
    .place_below  = wroc_wl_subsurface_place_below,
    .set_sync     = wroc_wl_subsurface_set_sync,
    .set_desync   = wroc_wl_subsurface_set_desync,
};
