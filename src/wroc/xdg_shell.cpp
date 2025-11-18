#include "server.hpp"
#include "util.hpp"

static
void wroc_xdg_wm_base_get_xdg_surface(wl_client* client, wl_resource* resource, u32 id, wl_resource* wl_surface)
{
    auto* new_resource = wl_resource_create(client, &xdg_surface_interface, wl_resource_get_version(resource), id);
    wroc_debug_track_resource(new_resource);
    auto* xdg_surface = new wroc_xdg_surface {};
    xdg_surface->resource = new_resource;
    xdg_surface->surface = wroc_get_userdata<wroc_surface>(wl_surface);
    xdg_surface->surface->role_addon = xdg_surface;
    wroc_resource_set_implementation_refcounted(new_resource, &wroc_xdg_surface_impl, xdg_surface);
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
    wroc_resource_set_implementation(new_resource, &wroc_xdg_wm_base_impl, static_cast<wroc_server*>(data));
};

// -----------------------------------------------------------------------------

static
void wroc_xdg_surface_get_toplevel(wl_client* client, wl_resource* resource, u32 id)
{
    auto* new_resource = wl_resource_create(client, &xdg_toplevel_interface, wl_resource_get_version(resource), id);
    wroc_debug_track_resource(new_resource);
    auto* xdg_toplevel = new wroc_xdg_toplevel {};
    xdg_toplevel->resource = new_resource;
    xdg_toplevel->base = wroc_get_userdata<wroc_xdg_surface>(resource);
    xdg_toplevel->base->xdg_role_addon = xdg_toplevel;
    wroc_resource_set_implementation_refcounted(new_resource, &wroc_xdg_toplevel_impl, xdg_toplevel);
}

static
void wroc_xdg_surface_set_window_geometry(wl_client* client, wl_resource* resource, i32 x, i32 y, i32 width, i32 height)
{
    auto* surface = wroc_get_userdata<wroc_xdg_surface>(resource);
    surface->pending.geometry = {{x, y}, {width, height}};
    surface->pending.committed |= wroc_xdg_surface_committed_state::geometry;
}

void wroc_xdg_surface::on_commit()
{
    if (xdg_role_addon) {
        xdg_role_addon->on_commit();
    }

    // Update geometry

    if (pending.committed >= wroc_xdg_surface_committed_state::geometry) {
        if (!pending.geometry.extent.x || !pending.geometry.extent.y) {
            log_warn("Zero size invalid geometry committed, treating as if geometry never set!");
            current.committed -= wroc_xdg_surface_committed_state::geometry;
            pending.committed -= wroc_xdg_surface_committed_state::geometry;
        } else {
            current.geometry = pending.geometry;
        }
    }

    current.committed |= pending.committed;
    pending = {};
}

wroc_xdg_surface::~wroc_xdg_surface()
{
    if (surface->role_addon == this) {
        surface->role_addon = nullptr;
    }
}

static
void wroc_xdg_surface_ack_configure(wl_client* client, wl_resource* resource, u32 serial)
{
    auto* xdg_surface = wroc_get_userdata<wroc_xdg_surface>(resource);

    if (serial == xdg_surface->sent_configure_serial) {
        log_info("Client acked configure: {}", serial);
        xdg_surface->acked_configure_serial = serial;

        if (xdg_surface->xdg_role_addon) {
            xdg_surface->xdg_role_addon->on_ack_configure(serial);
        }
    }
}

void wroc_xdg_surface_flush_configure(wroc_xdg_surface* xdg_surface)
{
    xdg_surface->sent_configure_serial = wl_display_next_serial(xdg_surface->surface->server->display);
    xdg_surface_send_configure(xdg_surface->resource, xdg_surface->sent_configure_serial);
}

const struct xdg_surface_interface wroc_xdg_surface_impl = {
    .destroy             = wroc_simple_resource_destroy_callback,
    .get_toplevel        = wroc_xdg_surface_get_toplevel,
    .get_popup           = WROC_STUB,
    .set_window_geometry = wroc_xdg_surface_set_window_geometry,
    .ack_configure       = wroc_xdg_surface_ack_configure,
};

rect2i32 wroc_xdg_surface_get_geometry(wroc_xdg_surface* xdg_surface)
{
    rect2i32 geom = {};
    if (xdg_surface->current.committed >= wroc_xdg_surface_committed_state::geometry) {
        geom = xdg_surface->current.geometry;
    } else if (xdg_surface->surface->current.buffer) {
        auto* buffer = xdg_surface->surface->current.buffer.get();
        geom.extent = { buffer->extent.x, buffer->extent.y };
    }
    return geom;
}

vec2i32 wroc_xdg_surface_get_position(wroc_xdg_surface* xdg_surface, rect2i32* p_geom)
{
    auto geom = wroc_xdg_surface_get_geometry(xdg_surface);
    if (p_geom) *p_geom = geom;

    auto geom_origin_pos = xdg_surface->anchor.position - (geom.extent * xdg_surface->anchor.relative);
    return geom_origin_pos - geom.origin;
}

// -----------------------------------------------------------------------------

static
void wroc_xdg_toplevel_set_title(wl_client* client, wl_resource* resource, const char* title)
{
    auto* toplevel = wroc_get_userdata<wroc_xdg_toplevel>(resource);
    toplevel->pending.title = title ? std::string{title} : std::string{};
    toplevel->pending.committed |= wroc_xdg_toplevel_committed_state::title;
}

static
void wroc_xdg_toplevel_set_app_id(wl_client* client, wl_resource* resource, const char* app_id)
{
    auto* toplevel = wroc_get_userdata<wroc_xdg_toplevel>(resource);
    toplevel->pending.app_id = app_id ? std::string{app_id} : std::string{};
    toplevel->pending.committed |= wroc_xdg_toplevel_committed_state::app_id;
}

static
void wroc_xdg_toplevel_move(wl_client* client, wl_resource* resource, wl_resource* seat, u32 serial)
{
    // TODO: Check serial

    auto* toplevel = wroc_get_userdata<wroc_xdg_toplevel>(resource);

    // TODO: Use seat to select pointer
    auto* pointer = toplevel->base->surface->server->seat->pointer;
    if (!std::ranges::contains(pointer->pressed, BTN_LEFT)) {
        log_warn("toplevel attempted to initiate move but left button was not pressed");
        return;
    }

    wroc_begin_move_interaction(toplevel, pointer, wroc_directions::horizontal | wroc_directions::vertical);
}

static
void wroc_xdg_toplevel_resize(wl_client* client, wl_resource* resource, wl_resource* seat, u32 serial, u32 edges)
{
    // TODO: Check serial

    auto* toplevel = wroc_get_userdata<wroc_xdg_toplevel>(resource);

    // TODO: Use seat to select pointer
    auto* pointer = toplevel->base->surface->server->seat->pointer;
    if (!std::ranges::contains(pointer->pressed, BTN_LEFT)) {
        log_warn("toplevel attempted to initiate resize but left button was not pressed");
        return;
    }

    vec2i32 anchor_rel = toplevel->base->anchor.relative;
    wroc_directions dirs = {};
    switch (edges) {
        break;case XDG_TOPLEVEL_RESIZE_EDGE_NONE: return;
        break;case XDG_TOPLEVEL_RESIZE_EDGE_TOP:          ; anchor_rel.y = 1;    dirs = wroc_directions::vertical;
        break;case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM:       ; anchor_rel.y = 0;    dirs = wroc_directions::vertical;
        break;case XDG_TOPLEVEL_RESIZE_EDGE_LEFT:         ; anchor_rel.x = 1;    dirs = wroc_directions::horizontal;
        break;case XDG_TOPLEVEL_RESIZE_EDGE_RIGHT:        ; anchor_rel.x = 0;    dirs = wroc_directions::horizontal;
        break;case XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT:     ; anchor_rel = {1, 1}; dirs = wroc_directions::horizontal | wroc_directions::vertical;
        break;case XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT:    ; anchor_rel = {0, 1}; dirs = wroc_directions::horizontal | wroc_directions::vertical;
        break;case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT:  ; anchor_rel = {1, 0}; dirs = wroc_directions::horizontal | wroc_directions::vertical;
        break;case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT: ; anchor_rel = {0, 0}; dirs = wroc_directions::horizontal | wroc_directions::vertical;
    }

    wroc_begin_resize_interaction(toplevel, pointer, anchor_rel, dirs);
}

static
void wroc_xdg_toplevel_on_initial_commit(wroc_xdg_toplevel* toplevel)
{
    wroc_xdg_toplevel_set_size(toplevel, {0, 0});
    wroc_xdg_toplevel_set_state(toplevel, XDG_TOPLEVEL_STATE_ACTIVATED, true);

    wroc_xdg_toplevel_flush_configure(toplevel);

    if (wl_resource_get_version(toplevel->resource) >= XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION) {
        xdg_toplevel_send_wm_capabilities(toplevel->resource, wrei_ptr_to(wroc_to_wl_array<const xdg_toplevel_wm_capabilities>({
            XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN,
            XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE,
        })));
    }

    xdg_surface_send_configure(toplevel->base->resource, wl_display_next_serial(toplevel->base->surface->server->display));
}

void wroc_xdg_toplevel::on_commit()
{
    if (initial_commit) {
        initial_commit = false;
        wroc_xdg_toplevel_on_initial_commit(this);
    }

    if (pending.committed >= wroc_xdg_toplevel_committed_state::title)  current.title  = pending.title;
    if (pending.committed >= wroc_xdg_toplevel_committed_state::app_id) current.app_id = pending.app_id;

    current.committed |= pending.committed;
    pending = {};
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
    .set_title        = wroc_xdg_toplevel_set_title,
    .set_app_id       = wroc_xdg_toplevel_set_app_id,
    .show_window_menu = WROC_STUB,
    .move             = wroc_xdg_toplevel_move,
    .resize           = wroc_xdg_toplevel_resize,
    .set_max_size     = WROC_STUB,
    .set_min_size     = WROC_STUB,
    .set_maximized    = WROC_STUB,
    .unset_maximized  = WROC_STUB,
    .set_fullscreen   = WROC_STUB,
    .unset_fullscreen = WROC_STUB,
    .set_minimized    = WROC_STUB,
};

void wroc_xdg_toplevel_set_size(wroc_xdg_toplevel* toplevel, vec2i32 size)
{
    if (toplevel->size == size) return;
    toplevel->size = size;
    toplevel->pending_configure = wroc_xdg_toplevel_configure_state::size;
}

void wroc_xdg_toplevel_set_bounds(wroc_xdg_toplevel* toplevel, vec2i32 bounds)
{
    if (wl_resource_get_version(toplevel->resource) >= XDG_TOPLEVEL_CONFIGURE_BOUNDS_SINCE_VERSION) {
        toplevel->bounds = bounds;
        toplevel->pending_configure |= wroc_xdg_toplevel_configure_state::bounds;
    }
}

void wroc_xdg_toplevel_set_state(wroc_xdg_toplevel* toplevel, xdg_toplevel_state state, bool enabled)
{
    if (enabled) {
        if (std::ranges::find(toplevel->states, state) == toplevel->states.end()) {
            toplevel->states.emplace_back(state);
            toplevel->pending_configure |= wroc_xdg_toplevel_configure_state::states;
        }
    } else if (std::erase(toplevel->states, state)) {
        toplevel->pending_configure |= wroc_xdg_toplevel_configure_state::states;
    }
}

void wroc_xdg_toplevel_flush_configure(wroc_xdg_toplevel* toplevel)
{
    if (toplevel->pending_configure == wroc_xdg_toplevel_configure_state::none) return;
    if (toplevel->base->sent_configure_serial > toplevel->base->acked_configure_serial) {
        log_warn("Waiting for client ack before reconfiguring");
        return;
    }

    if (toplevel->pending_configure >= wroc_xdg_toplevel_configure_state::bounds) {
        xdg_toplevel_send_configure_bounds(toplevel->resource, toplevel->bounds.x, toplevel->bounds.y);
    }

    xdg_toplevel_send_configure(toplevel->resource, toplevel->size.x, toplevel->size.y,
        wrei_ptr_to(wroc_to_wl_array<xdg_toplevel_state>(toplevel->states)));

    wroc_xdg_surface_flush_configure(toplevel->base.get());

    toplevel->pending_configure = {};
}

void wroc_xdg_toplevel::on_ack_configure(u32 serial)
{
    wroc_xdg_toplevel_flush_configure(this);
}
