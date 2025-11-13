#include "server.hpp"
#include "util.hpp"

static
void wroc_xdg_wm_base_get_xdg_surface(wl_client* client, wl_resource* resource, u32 id, wl_resource* wl_surface)
{
    auto* new_resource = wl_resource_create(client, &xdg_surface_interface, wl_resource_get_version(resource), id);
    wroc_debug_track_resource(new_resource);
    auto* xdg_surface = new wroc_xdg_surface {};
    xdg_surface->xdg_surface = new_resource;
    xdg_surface->surface = wroc_get_userdata<wroc_surface>(wl_surface);
    xdg_surface->surface->role_addon = xdg_surface;
    wl_resource_set_implementation(new_resource, &wroc_xdg_surface_impl, xdg_surface, WROC_SIMPLE_RESOURCE_UNREF(wroc_xdg_surface, xdg_surface));
}

const struct xdg_wm_base_interface wroc_xdg_wm_base_impl = {
    .create_positioner = WROC_STUB,
    .destroy           = wroc_simple_resource_destroy_callback,
    .get_xdg_surface   = wroc_xdg_wm_base_get_xdg_surface,
    .pong              = WROC_STUB,
};

void wroc_xdg_wm_base_bind_global(wl_client* client, void* data, u32 version, u32 id)
{
    auto* new_resource = wl_resource_create(client, &xdg_wm_base_interface, version, id);
    wroc_debug_track_resource(new_resource);
    auto* wm_base = new wroc_xdg_wm_base {};
    wm_base->server = static_cast<wroc_server*>(data);
    wm_base->xdg_wm_base = new_resource;
    wl_resource_set_implementation(new_resource, &wroc_xdg_wm_base_impl, wm_base, WROC_SIMPLE_RESOURCE_UNREF(wroc_xdg_wm_base, xdg_wm_base));
};

// -----------------------------------------------------------------------------

static
void wroc_xdg_surface_get_toplevel(wl_client* client, wl_resource* resource, u32 id)
{
    auto* new_resource = wl_resource_create(client, &xdg_toplevel_interface, wl_resource_get_version(resource), id);
    wroc_debug_track_resource(new_resource);
    auto* xdg_toplevel = new wroc_xdg_toplevel {};
    xdg_toplevel->xdg_toplevel = new_resource;
    xdg_toplevel->base = wroc_get_userdata<wroc_xdg_surface>(resource);
    xdg_toplevel->base->xdg_role_addon = xdg_toplevel;
    wl_resource_set_implementation(new_resource, &wroc_xdg_toplevel_impl, xdg_toplevel, WROC_SIMPLE_RESOURCE_UNREF(wroc_xdg_toplevel, xdg_toplevel));
}

static
void wroc_xdg_surface_set_window_geometry(wl_client* client, wl_resource* resource, i32 x, i32 y, i32 width, i32 height)
{
    auto* surface = wroc_get_userdata<wroc_xdg_surface>(resource);
    surface->pending.geometry = {{x, y}, {width, height}};
}

void wroc_xdg_surface::on_initial_commit()
{
    if (xdg_role_addon) {
        xdg_role_addon->on_initial_commit();
    }

    xdg_surface_send_configure(xdg_surface, wl_display_next_serial(surface->server->display));
}

void wroc_xdg_surface::on_commit()
{
    if (xdg_role_addon) {
        xdg_role_addon->on_commit();
    }

    // Update geometry

    if (pending.geometry) {
        if (!pending.geometry->extent.x || !pending.geometry->extent.y) {
            log_warn("Zero size invalid geometry committed, treating as if geometry never set!");
        } else {
            current.geometry = *pending.geometry;
        }
        pending.geometry = std::nullopt;
    }

    if (current.geometry) {
        log_debug("Geometry: (({}, {}), ({}, {}))",
            current.geometry->origin.x, current.geometry->origin.y,
            current.geometry->extent.x, current.geometry->extent.y);
    }
}

wroc_xdg_surface::~wroc_xdg_surface()
{
    if (surface->role_addon == this) {
        surface->role_addon = nullptr;
    }
}

const struct xdg_surface_interface wroc_xdg_surface_impl = {
    .destroy             = wroc_simple_resource_destroy_callback,
    .get_toplevel        = wroc_xdg_surface_get_toplevel,
    .get_popup           = WROC_STUB,
    .set_window_geometry = wroc_xdg_surface_set_window_geometry,
    .ack_configure       = WROC_STUB,
};

// -----------------------------------------------------------------------------

void wroc_xdg_toplevel::on_initial_commit()
{
    wrei_vec2i32 initial_bounds = { 1280, 720 };

    if (wl_resource_get_version(xdg_toplevel) >= XDG_TOPLEVEL_CONFIGURE_BOUNDS_SINCE_VERSION) {
        xdg_toplevel_send_configure_bounds(xdg_toplevel, initial_bounds.x, initial_bounds.y);
    }

    xdg_toplevel_send_configure(xdg_toplevel, initial_bounds.x, initial_bounds.y,
        wrei_ptr_to(wroc_to_wl_array<const xdg_toplevel_state>({ XDG_TOPLEVEL_STATE_ACTIVATED })));

    if (wl_resource_get_version(xdg_toplevel) >= XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION) {
        xdg_toplevel_send_wm_capabilities(xdg_toplevel, wrei_ptr_to(wroc_to_wl_array<const xdg_toplevel_wm_capabilities>({
            XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN,
            XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE,
        })));
    }
}

void wroc_xdg_toplevel::on_commit()
{
}

wroc_xdg_toplevel::~wroc_xdg_toplevel()
{
    if (base->xdg_role_addon == this) {
        base->xdg_role_addon = nullptr;
    }
}

const struct xdg_toplevel_interface wroc_xdg_toplevel_impl = {
    .destroy          = wroc_simple_resource_destroy_callback,
    .set_parent       = WROC_STUB,
    .set_title        = WROC_STUB,
    .set_app_id       = WROC_STUB,
    .show_window_menu = WROC_STUB,
    .move             = WROC_STUB,
    .resize           = WROC_STUB,
    .set_max_size     = WROC_STUB,
    .set_min_size     = WROC_STUB,
    .set_maximized    = WROC_STUB,
    .unset_maximized  = WROC_STUB,
    .set_fullscreen   = WROC_STUB,
    .unset_fullscreen = WROC_STUB,
    .set_minimized    = WROC_STUB,
};
