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
    subsurface->wl_subsurface = new_resource;
    subsurface->parent = wrei_weak_from(wroc_get_userdata<wroc_surface>(parent));
    subsurface->parent->subsurfaces.emplace_back(subsurface);
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
    log_warn("Subsurface position set: ({}, {})", x, y);

    auto* subsurface = wroc_get_userdata<wroc_subsurface>(resource);
    subsurface->position = {x, y};
}

void wroc_subsurface::on_parent_commit()
{
    if (is_synchronized()) {
        wroc_surface_commit(surface.get());
    }
}

void wroc_subsurface::on_commit()
{
}

bool wroc_subsurface::is_synchronized()
{
    if (synchronized) return true;
    return parent && parent->role_addon && parent->role_addon->is_synchronized();
}

wroc_subsurface::~wroc_subsurface()
{
    if (parent) {
        std::erase(parent->subsurfaces, this);
    }
}

const struct wl_subsurface_interface wroc_wl_subsurface_impl = {
    .destroy      = wroc_simple_resource_destroy_callback,
    .set_position = wroc_wl_subsurface_set_position,
    .place_above  = WROC_STUB,
    .place_below  = WROC_STUB,
    .set_sync     = WROC_STUB,
    .set_desync   = WROC_STUB,
};
