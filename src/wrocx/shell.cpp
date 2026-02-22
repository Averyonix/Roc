#include "internal.hpp"

WROC_NAMESPACE_BEGIN

static
void get_xdg_surface(wl_client* client, wl_resource* resource, u32 id, wl_resource* wl_surface)
{
    auto* surface = wroc_get_userdata<wroc_surface>(wl_surface);
    surface->xdg_surface = wroc_resource_create_refcounted(xdg_surface, client, resource, id, surface);
}

WROC_INTERFACE(xdg_wm_base) = {
    WROC_STUB(destroy),
    WROC_STUB(create_positioner),
    .get_xdg_surface = get_xdg_surface,
    WROC_STUB(pong),
};

WROC_BIND_GLOBAL(xdg_wm_base)
{
    wroc_resource_create(xdg_wm_base, client, version, id, wroc_get_userdata<wroc_server>(data));
}

void wroc_xdg_surface_apply(wroc_surface* surface, wroc_surface_state& from)
{
    WROC_ADDON_SIMPLE_STATE_APPLY(from, surface->current, xdg.geometry,     geometry);
    WROC_ADDON_SIMPLE_STATE_APPLY(from, surface->current, xdg.acked_serial, acked_serial);
}

static
void configure(wroc_surface* surface)
{
    surface->sent_serial = wroc_next_serial(surface->server);
    wroc_send(surface->server, xdg_surface_send_configure, surface->xdg_surface, surface->sent_serial);
}

// -----------------------------------------------------------------------------

static
void get_toplevel(wl_client* client, wl_resource* resource, u32 id)
{
    auto* surface = wroc_get_userdata<wroc_surface>(resource);
    surface->role = wroc_surface_role::xdg_toplevel;
    surface->xdg_toplevel = wroc_resource_create_refcounted(xdg_toplevel, client, resource, id, surface);
}

static
void ack_configure(wl_client* client, wl_resource* resource, u32 serial)
{
    auto* surface = wroc_get_userdata<wroc_surface>(resource);
    auto* server = surface->server;

    if (serial > surface->sent_serial) {
        wroc_post_error(server, surface->xdg_surface, XDG_SURFACE_ERROR_INVALID_SERIAL,
            "Client acked configure {} which is higher than latest sent configure serial {}",
            serial, surface->sent_serial);
        return;
    }

    if (serial <= surface->acked_serial) {
        log_warn("Client acked old configure serial");
        return;
    }

    surface->pending->xdg.acked_serial = serial;
    surface->pending->committed.insert(wroc_surface_committed_state::acked_serial);

    surface->acked_serial = serial;
}

WROC_INTERFACE(xdg_surface) = {
    WROC_STUB(destroy),
    .get_toplevel = get_toplevel,
    WROC_STUB(get_popup),
    .set_window_geometry = WROC_ADDON_SIMPLE_STATE_REQUEST(wroc_xdg_surface, xdg.geometry, geometry, rect2i32({x, y}, {w, h}, wrei_xywh), i32 x, i32 y, i32 w, i32 h),
    .ack_configure = ack_configure,
};

static
void send_premap_configure(wroc_surface* surface)
{
    auto* server = surface->server;

    if (wl_resource_get_version(surface->xdg_toplevel) >= XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION) {
        wroc_send(server, xdg_toplevel_send_wm_capabilities, surface->xdg_toplevel, wrei_ptr_to(wroc_to_wl_array<const xdg_toplevel_wm_capabilities>({
            XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN,
        })));
    }

    wroc_send(server, xdg_toplevel_send_configure, surface->xdg_toplevel,
        0, 0,
        wrei_ptr_to(wroc_to_wl_array<const xdg_toplevel_state>({
            XDG_TOPLEVEL_STATE_ACTIVATED,
        }))
    );

    configure(surface);
}

void wroc_toplevel_apply(wroc_surface* surface, wroc_surface_state& from)
{
    WROC_ADDON_SIMPLE_STATE_APPLY(from, surface->current, toplevel.title, title);
    WROC_ADDON_SIMPLE_STATE_APPLY(from, surface->current, toplevel.app_id, app_id);
    WROC_ADDON_SIMPLE_STATE_APPLY(from, surface->current, toplevel.max_size, max_size);
    WROC_ADDON_SIMPLE_STATE_APPLY(from, surface->current, toplevel.min_size, min_size);

    if (!surface->mapped) {
        log_info("toplevel surface committed but not mapped, sending configure");
        send_premap_configure(surface);
    }
}

// -----------------------------------------------------------------------------

WROC_INTERFACE(xdg_toplevel) = {
    WROC_STUB(destroy),
    WROC_STUB(set_parent),
    .set_title  = WROC_ADDON_SIMPLE_STATE_REQUEST(wroc_toplevel, toplevel.title,  title,  title,  const char* title),
    .set_app_id = WROC_ADDON_SIMPLE_STATE_REQUEST(wroc_toplevel, toplevel.app_id, app_id, app_id, const char* app_id),
    WROC_STUB(show_window_menu),
    WROC_STUB(move),
    WROC_STUB(resize),
    .set_max_size = WROC_ADDON_SIMPLE_STATE_REQUEST(wroc_toplevel, toplevel.max_size, max_size, vec2i32(w, h), i32 w, i32 h),
    .set_min_size = WROC_ADDON_SIMPLE_STATE_REQUEST(wroc_toplevel, toplevel.min_size, min_size, vec2i32(w, h), i32 w, i32 h),
    WROC_STUB(set_maximized),
    WROC_STUB(unset_maximized),
    WROC_STUB(set_fullscreen),
    WROC_STUB(unset_fullscreen),
    WROC_STUB(set_minimized),
};

WROC_NAMESPACE_END
