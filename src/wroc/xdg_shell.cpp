#include "server.hpp"
#include "util.hpp"

const u32 wroc_xdg_wm_base_version = 7;

static
void wroc_xdg_wm_base_create_positioner(wl_client* client, wl_resource* resource, u32 id);

static
void wroc_xdg_wm_base_get_xdg_surface(wl_client* client, wl_resource* resource, u32 id, wl_resource* wl_surface)
{
    auto* new_resource = wroc_resource_create(client, &xdg_surface_interface, wl_resource_get_version(resource), id);
    auto* surface = wroc_get_userdata<wroc_surface>(wl_surface);;
    auto* xdg_surface = wrei_create_unsafe<wroc_xdg_surface>();
    xdg_surface->resource = new_resource;
    wroc_surface_put_addon(surface, xdg_surface);
    wroc_resource_set_implementation_refcounted(new_resource, &wroc_xdg_surface_impl, xdg_surface);
}

const struct xdg_wm_base_interface wroc_xdg_wm_base_impl = {
    .destroy           = wroc_simple_resource_destroy_callback,
    .create_positioner = wroc_xdg_wm_base_create_positioner,
    .get_xdg_surface   = wroc_xdg_wm_base_get_xdg_surface,
    WROC_STUB(pong),
};

void wroc_xdg_wm_base_bind_global(wl_client* client, void* data, u32 version, u32 id)
{
    auto* new_resource = wroc_resource_create(client, &xdg_wm_base_interface, version, id);
    wroc_resource_set_implementation(new_resource, &wroc_xdg_wm_base_impl, static_cast<wroc_server*>(data));
};

// -----------------------------------------------------------------------------

static
void wroc_xdg_surface_get_toplevel(wl_client* client, wl_resource* resource, u32 id)
{
    auto* new_resource = wroc_resource_create(client, &xdg_toplevel_interface, wl_resource_get_version(resource), id);
    auto* xdg_surface = wroc_get_userdata<wroc_xdg_surface>(resource);
    auto* xdg_toplevel = wrei_create_unsafe<wroc_toplevel>();
    xdg_toplevel->resource = new_resource;
    wroc_surface_put_addon(xdg_surface->surface.get(), xdg_toplevel);
    wroc_resource_set_implementation_refcounted(new_resource, &wroc_xdg_toplevel_impl, xdg_toplevel);
}

static
void wroc_xdg_surface_set_window_geometry(wl_client* client, wl_resource* resource, i32 x, i32 y, i32 width, i32 height)
{
    auto* surface = wroc_get_userdata<wroc_xdg_surface>(resource);
    surface->pending.geometry = {{x, y}, {width, height}};
    surface->pending.committed |= wroc_xdg_surface_committed_state::geometry;
}

void wroc_xdg_surface::on_commit(wroc_surface_commit_flags flags)
{
    // TODO: Order of addon commit updates

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

static
void wroc_xdg_surface_ack_configure(wl_client* client, wl_resource* resource, u32 serial)
{
    auto* xdg_surface = wroc_get_userdata<wroc_xdg_surface>(resource);

    if (serial == xdg_surface->sent_configure_serial) {
        log_info("Client acked configure: {}", serial);
        xdg_surface->acked_configure_serial = serial;

        for (auto& a : xdg_surface->surface->addons) {
            if (a.get() != xdg_surface) {
                a->on_ack_configure(serial);
            }
        }
    }
}

void wroc_xdg_surface_flush_configure(wroc_xdg_surface* xdg_surface)
{
    xdg_surface->sent_configure_serial = wl_display_next_serial(xdg_surface->surface->server->display);
    wroc_send(xdg_surface_send_configure, xdg_surface->resource, xdg_surface->sent_configure_serial);
}

static
void wroc_xdg_surface_get_popup(wl_client* client, wl_resource* resource, u32 id, wl_resource* parent, wl_resource* positioner);

const struct xdg_surface_interface wroc_xdg_surface_impl = {
    .destroy             = wroc_surface_addon_destroy,
    .get_toplevel        = wroc_xdg_surface_get_toplevel,
    .get_popup           = wroc_xdg_surface_get_popup,
    .set_window_geometry = wroc_xdg_surface_set_window_geometry,
    .ack_configure       = wroc_xdg_surface_ack_configure,
};

rect2i32 wroc_xdg_surface_get_geometry(wroc_xdg_surface* xdg_surface)
{
    return (xdg_surface->current.committed >= wroc_xdg_surface_committed_state::geometry)
        ? xdg_surface->current.geometry
        : xdg_surface->surface->buffer_dst;
}

// -----------------------------------------------------------------------------

static
void wroc_xdg_toplevel_set_title(wl_client* client, wl_resource* resource, const char* title)
{
    auto* toplevel = wroc_get_userdata<wroc_toplevel>(resource);
    toplevel->pending.title = title ? std::string{title} : std::string{};
    toplevel->pending.committed |= wroc_xdg_toplevel_committed_state::title;
}

static
void wroc_xdg_toplevel_set_app_id(wl_client* client, wl_resource* resource, const char* app_id)
{
    auto* toplevel = wroc_get_userdata<wroc_toplevel>(resource);
    toplevel->pending.app_id = app_id ? std::string{app_id} : std::string{};
    toplevel->pending.committed |= wroc_xdg_toplevel_committed_state::app_id;
}

static
void wroc_xdg_toplevel_move(wl_client* client, wl_resource* resource, wl_resource* seat, u32 serial)
{
    // TODO: Check serial

    auto* toplevel = wroc_get_userdata<wroc_toplevel>(resource);

    // TODO: Use seat to select pointer
    auto* pointer = toplevel->surface->server->seat->pointer.get();
    if (!pointer->pressed.contains(BTN_LEFT)) {
        log_warn("toplevel attempted to initiate move but left button was not pressed");
        return;
    }

    wroc_begin_move_interaction(toplevel, pointer, wroc_directions::horizontal | wroc_directions::vertical);
}

static
void wroc_xdg_toplevel_resize(wl_client* client, wl_resource* resource, wl_resource* seat, u32 serial, u32 edges)
{
    // TODO: Check serial

    auto* toplevel = wroc_get_userdata<wroc_toplevel>(resource);

    // TODO: Use seat to select pointer
    auto* pointer = toplevel->surface->server->seat->pointer.get();
    if (!pointer->pressed.contains(BTN_LEFT)) {
        log_warn("toplevel attempted to initiate resize but left button was not pressed");
        return;
    }

    vec2i32 anchor_rel = toplevel->base()->anchor.relative;
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
void wroc_xdg_toplevel_on_initial_commit(wroc_toplevel* toplevel)
{
    wroc_xdg_toplevel_set_size(toplevel, {0, 0});
    wroc_xdg_toplevel_set_state(toplevel, XDG_TOPLEVEL_STATE_ACTIVATED, true);

    wroc_xdg_toplevel_flush_configure(toplevel);

    if (wl_resource_get_version(toplevel->resource) >= XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION) {
        wroc_send(xdg_toplevel_send_wm_capabilities, toplevel->resource, wrei_ptr_to(wroc_to_wl_array<const xdg_toplevel_wm_capabilities>({
            XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN,
            XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE,
        })));
    }

    wroc_xdg_surface_flush_configure(toplevel->base());
}

void wroc_toplevel::on_commit(wroc_surface_commit_flags)
{
    if (!initial_configure_complete) {
        initial_configure_complete = true;
        wroc_xdg_toplevel_on_initial_commit(this);
    }
    else if (!initial_size_receieved) {
        initial_size_receieved = true;
        auto geom = wroc_xdg_surface_get_geometry(base());
        log_debug("Initial surface size: {}", wrei_to_string(geom.extent));
        wroc_xdg_toplevel_set_size(this, geom.extent);
        wroc_xdg_toplevel_flush_configure(this);
    }

    if (pending.committed >= wroc_xdg_toplevel_committed_state::title)  current.title  = pending.title;
    if (pending.committed >= wroc_xdg_toplevel_committed_state::app_id) current.app_id = pending.app_id;

    current.committed |= pending.committed;
    pending = {};
}

const struct xdg_toplevel_interface wroc_xdg_toplevel_impl = {
    .destroy    = wroc_surface_addon_destroy,
    WROC_STUB(set_parent),
    .set_title  = wroc_xdg_toplevel_set_title,
    .set_app_id = wroc_xdg_toplevel_set_app_id,
    WROC_STUB(show_window_menu),
    .move       = wroc_xdg_toplevel_move,
    .resize     = wroc_xdg_toplevel_resize,
    WROC_STUB(set_max_size),
    WROC_STUB(set_min_size),
    WROC_STUB(set_maximized),
    WROC_STUB(unset_maximized),
    WROC_STUB(set_fullscreen),
    WROC_STUB(unset_fullscreen),
    WROC_STUB(set_minimized),
};

void wroc_xdg_toplevel_set_size(wroc_toplevel* toplevel, vec2i32 size)
{
    if (toplevel->size == size) return;
    toplevel->size = size;
    toplevel->pending_configure = wroc_xdg_toplevel_configure_state::size;
}

void wroc_xdg_toplevel_set_bounds(wroc_toplevel* toplevel, vec2i32 bounds)
{
    if (wl_resource_get_version(toplevel->resource) >= XDG_TOPLEVEL_CONFIGURE_BOUNDS_SINCE_VERSION) {
        toplevel->bounds = bounds;
        toplevel->pending_configure |= wroc_xdg_toplevel_configure_state::bounds;
    }
}

void wroc_xdg_toplevel_set_state(wroc_toplevel* toplevel, xdg_toplevel_state state, bool enabled)
{
    if (enabled) {
        if (!std::ranges::contains(toplevel->states, state)) {
            toplevel->states.emplace_back(state);
            toplevel->pending_configure |= wroc_xdg_toplevel_configure_state::states;
        }
    } else if (std::erase(toplevel->states, state)) {
        toplevel->pending_configure |= wroc_xdg_toplevel_configure_state::states;
    }
}

void wroc_xdg_toplevel_flush_configure(wroc_toplevel* toplevel)
{
    if (toplevel->pending_configure == wroc_xdg_toplevel_configure_state::none) return;
    if (toplevel->base()->sent_configure_serial > toplevel->base()->acked_configure_serial) {
        log_warn("Waiting for client ack before reconfiguring");
        return;
    }

    if (toplevel->pending_configure >= wroc_xdg_toplevel_configure_state::bounds) {
        wroc_send(xdg_toplevel_send_configure_bounds, toplevel->resource, toplevel->bounds.x, toplevel->bounds.y);
    }

    wroc_send(xdg_toplevel_send_configure, toplevel->resource, toplevel->size.x, toplevel->size.y,
        wrei_ptr_to(wroc_to_wl_array<xdg_toplevel_state>(toplevel->states)));

    wroc_xdg_surface_flush_configure(toplevel->base());

    toplevel->pending_configure = {};
}

void wroc_toplevel::on_ack_configure(u32 serial)
{
    wroc_xdg_toplevel_flush_configure(this);
}

// -----------------------------------------------------------------------------

void wroc_xdg_wm_base_create_positioner(wl_client* client, wl_resource* resource, u32 id)
{
    auto* new_resource = wroc_resource_create(client, &xdg_positioner_interface, wl_resource_get_version(resource), id);
    auto* server = wroc_get_userdata<wroc_server>(resource);
    auto* positioner = wrei_create_unsafe<wroc_positioner>();
    positioner->resource = new_resource;
    positioner->server = server;
    wroc_resource_set_implementation_refcounted(new_resource, &wroc_xdg_positioner_impl, positioner);
}

static
void wroc_xdg_positioner_set_size(wl_client* client, wl_resource* resource, i32 width, i32 height)
{
    wroc_get_userdata<wroc_positioner>(resource)->rules.size = {width, height};
}

static
void wroc_xdg_positioner_set_anchor_rect(wl_client* client, wl_resource* resource, i32 x, i32 y, i32 width, i32 height)
{
    wroc_get_userdata<wroc_positioner>(resource)->rules.anchor_rect = {{x, y}, {width, height}};
}

static
void wroc_xdg_positioner_set_anchor(wl_client* client, wl_resource* resource, u32 anchor)
{
    wroc_get_userdata<wroc_positioner>(resource)->rules.anchor = xdg_positioner_anchor(anchor);
}

static
void wroc_xdg_positioner_set_gravity(wl_client* client, wl_resource* resource, u32 gravity)
{
    wroc_get_userdata<wroc_positioner>(resource)->rules.gravity = xdg_positioner_gravity(gravity);
}

static
void wroc_xdg_positioner_set_constraint_adjustment(wl_client* client, wl_resource* resource, u32 constraint_adjustment)
{
    wroc_get_userdata<wroc_positioner>(resource)->rules.constraint_adjustment = xdg_positioner_constraint_adjustment(constraint_adjustment);
}

static
void wroc_xdg_positioner_set_offset(wl_client* client, wl_resource* resource, i32 x, i32 y)
{
    wroc_get_userdata<wroc_positioner>(resource)->rules.offset = {x, y};
}

static
void wroc_xdg_positioner_set_reactive(wl_client* client, wl_resource* resource)
{
    wroc_get_userdata<wroc_positioner>(resource)->rules.reactive = true;
}

static
void wroc_xdg_positioner_set_parent_size(wl_client* client, wl_resource* resource, i32 width, i32 height)
{
    wroc_get_userdata<wroc_positioner>(resource)->rules.parent_size = {width, height};
}

static
void wroc_xdg_positioner_set_parent_configure(wl_client* client, wl_resource* resource, u32 serial)
{
    wroc_get_userdata<wroc_positioner>(resource)->rules.parent_configure = serial;
}

const struct xdg_positioner_interface wroc_xdg_positioner_impl = {
    .destroy                   = wroc_simple_resource_destroy_callback,
    .set_size                  = wroc_xdg_positioner_set_size,
    .set_anchor_rect           = wroc_xdg_positioner_set_anchor_rect,
    .set_anchor                = wroc_xdg_positioner_set_anchor,
    .set_gravity               = wroc_xdg_positioner_set_gravity,
    .set_constraint_adjustment = wroc_xdg_positioner_set_constraint_adjustment,
    .set_offset                = wroc_xdg_positioner_set_offset,
    .set_reactive              = wroc_xdg_positioner_set_reactive,
    .set_parent_size           = wroc_xdg_positioner_set_parent_size,
    .set_parent_configure      = wroc_xdg_positioner_set_parent_configure,
};

struct wroc_xdg_positioner_axis_rules
{
    wroc_axis_region anchor;
    i32 size;
    i32 gravity;
    bool flip;
    bool slide;
    bool resize;
};

static
wroc_axis_region wroc_xdg_positioner_apply_axis(const wroc_xdg_positioner_axis_rules& rules, wroc_axis_region constraint)
{
    log_debug("wroc_xdg_position_apply_axis");
    log_debug("  constraint = ({} ; {})", constraint.pos, constraint.size);
    log_debug("  anchor = ({} ; {})", rules.anchor.pos, rules.anchor.size);
    log_debug("  size, gravity  = {}, {}", rules.size, rules.gravity);
    log_debug("  flip   = {}", rules.flip);
    log_debug("  slide  = {}", rules.slide);
    log_debug("  resize = {}", rules.resize);

    auto get_position = [](auto& rules) -> wroc_axis_region {
        return {rules.anchor.pos + rules.gravity - rules.size, rules.size};
    };

    auto get_overlaps = [&](wroc_axis_region region) -> wroc_axis_overlaps {
        return {constraint.pos - region.pos, region.pos + region.size - (constraint.pos + constraint.size)};
    };

    auto is_unconstrained = [&](wroc_axis_region region) {
        auto overlaps = get_overlaps(region);
        return overlaps.start <= 0 && overlaps.end <= 0;
    };

    auto region = get_position(rules);
    log_debug("  position = ({} ; {})", region.pos, region.size);

    if (is_unconstrained(region)) {
        log_debug("  already unconstrained!");
        return region;
    }

#define WROC_XDG_POSITIONER_IGNORE_FLIP 0
#if    !WROC_XDG_POSITIONER_IGNORE_FLIP
    if (rules.flip) {
        auto flipped_rules = rules;
        flipped_rules.anchor.pos = rules.anchor.size - rules.anchor.pos;
        flipped_rules.gravity = rules.size - rules.gravity;
        auto flipped = get_position(flipped_rules);
        log_debug("  flipped = ({} ; {})", flipped.pos, flipped.size);
        if (is_unconstrained(flipped)) {
            log_debug("  unconstrained by flip!");
            return flipped;
        }
    }
#endif

#define WROC_XDG_POSITIONER_ALWAYS_SLIDE 0
#if    !WROC_XDG_POSITIONER_ALWAYS_SLIDE
    if (rules.slide)
#endif
    {
        auto overlap = get_overlaps(region);
        log_debug("  attempting slide, overlaps: ({}, {})", overlap.start, overlap.end);
        if (overlap.start > 0 && overlap.end > 0) {
            log_debug("    overlaps both");
            // Overlaps both at start and end, move in direction of gravity until opposite edge is unconstrained
            if (rules.gravity == rules.size) {
                log_debug("    slide forward");
                region.pos += overlap.start;
            } else if (rules.gravity == 0) {
                log_debug("    slide back");
                region.pos -= overlap.end;
            } else {
                log_debug("    no gravity, can't slide");
            }
        } else if (overlap.start > 0) {
            // Overlaps at start, move forward
            region.pos += std::min(overlap.start, -overlap.end);
            log_debug("    overlaps start, move forward: {}", region.pos);
            return region;
        } else if (overlap.end > 0) {
            // Overlaps at end, move backward
            region.pos -= std::min(overlap.end, -overlap.start);
            log_debug("    overlaps start, move back: {}", region.pos);
            return region;
        }
    }

    if (rules.resize) {
        auto overlap = get_overlaps(region);
        log_debug("  attempting resize, overlaps: ({}, {})", overlap.start, overlap.end);
        if (overlap.start > 0 && overlap.end > 0) {
            log_debug("    overlaps both sides, constraint to fit exactly");
            // Overlaps both, fit constraint exactly
            region = constraint;
        } else if (overlap.start > 0 && overlap.start < region.size) {
            // Overlaps start by less than size, clip start
            region.pos  += overlap.start;
            region.size -= overlap.start;
            log_debug("    overlaps start, clipping ({}, {})", region.pos, region.size);
        } else if (overlap.end > 0 && overlap.end < region.size) {
            // Overlaps end by less than size, clip end
            region.size -= overlap.end;
            log_debug("    overlaps end, clipping ({}, {})", region.pos, region.size);
        } else {
            log_debug("    outside of region entirely");
        }
    }

    // Return best effort

    return region;
}

#define WROC_WAYLAND_EDGES_TO_REL_CASES(Prefix) \
    case Prefix##_NONE:         return {rel.x / 2, rel.y / 2}; \
    case Prefix##_TOP:          return {rel.x / 2, 0        }; \
    case Prefix##_BOTTOM:       return {rel.x / 2, rel.y    }; \
    case Prefix##_LEFT:         return {0,         rel.y / 2}; \
    case Prefix##_RIGHT:        return {rel.x,     rel.y / 2}; \
    case Prefix##_TOP_LEFT:     return {0,         0        }; \
    case Prefix##_TOP_RIGHT:    return {rel.x,     0        }; \
    case Prefix##_BOTTOM_LEFT:  return {0,         rel.y    }; \
    case Prefix##_BOTTOM_RIGHT: return {rel.x,     rel.y    };

template<typename T>
wrei_vec<2, T> wroc_xdg_positioner_anchor_to_rel(xdg_positioner_anchor anchor, wrei_vec<2, T> rel)
{
    switch (anchor) {
        WROC_WAYLAND_EDGES_TO_REL_CASES(XDG_POSITIONER_ANCHOR)
    }
}

template<typename T>
wrei_vec<2, T> wroc_xdg_positioner_gravity_to_rel(xdg_positioner_gravity gravity, wrei_vec<2, T> rel)
{
    switch (gravity) {
        WROC_WAYLAND_EDGES_TO_REL_CASES(XDG_POSITIONER_GRAVITY)
    }
}

static
void wroc_xdg_positioner_apply_axis_from_rules(const wroc_positioner_rules& rules, rect2i32 constraint, rect2i32& target, u32 axis)
{
    auto anchor = wroc_xdg_positioner_anchor_to_rel(rules.anchor, rules.anchor_rect.extent);
    auto gravity = wroc_xdg_positioner_gravity_to_rel(rules.gravity, rules.size);
    static constexpr std::array flip_adjustments   = { XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X,   XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y   };
    static constexpr std::array slide_adjustments  = { XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X,  XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y  };
    static constexpr std::array resize_adjustments = { XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_RESIZE_X, XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_RESIZE_Y };
    wroc_xdg_positioner_axis_rules axis_rules {
        .anchor = {
            .pos = anchor[axis],
            .size = rules.anchor_rect.extent[axis],
        },
        .size = rules.size[axis],
        .gravity = gravity[axis],
        .flip   = bool(rules.constraint_adjustment & flip_adjustments[axis]),
        .slide  = bool(rules.constraint_adjustment & slide_adjustments[axis]),
        .resize = bool(rules.constraint_adjustment & resize_adjustments[axis]),
    };
    auto offset = rules.anchor_rect.origin[axis] + rules.offset[axis];
    wroc_axis_region constraint_region {
        .pos = constraint.origin[axis] - offset,
        .size = constraint.extent[axis],
    };
    auto region = wroc_xdg_positioner_apply_axis(axis_rules, constraint_region);
    log_debug("  final position: ({}, {}), offset = {}", region.pos, region.size, offset);
    target.origin[axis] = region.pos + offset;
    target.extent[axis] = region.size;
}

static
rect2i32 wroc_xdg_positioner_apply(const wroc_positioner_rules& rules, rect2i32 constraint)
{
    rect2i32 target;
    wroc_xdg_positioner_apply_axis_from_rules(rules, constraint, target, 0);
    wroc_xdg_positioner_apply_axis_from_rules(rules, constraint, target, 1);
    return target;
}

// -----------------------------------------------------------------------------

void wroc_xdg_surface_get_popup(wl_client* client, wl_resource* resource, u32 id, wl_resource* _parent, wl_resource* positioner)
{
    auto* new_resource = wroc_resource_create(client, &xdg_popup_interface, wl_resource_get_version(resource), id);
    auto* base = wroc_get_userdata<wroc_xdg_surface>(resource);
    auto* popup = wrei_create_unsafe<wroc_popup>();
    popup->resource = new_resource;
    wroc_surface_put_addon(base->surface.get(), popup);

    auto* xdg_positioner = wroc_get_userdata<wroc_positioner>(positioner);

    popup->positioner = xdg_positioner;
    popup->parent = wroc_get_userdata<wroc_xdg_surface>(_parent);
    if (popup->parent) {
        if (wroc_toplevel* toplevel = wroc_surface_get_addon<wroc_toplevel>(popup->parent->surface.get())) {
            popup->root_toplevel = toplevel;
        } else if (wroc_popup* parent_popup = wroc_surface_get_addon<wroc_popup>(popup->parent->surface.get())) {
            popup->root_toplevel = parent_popup->root_toplevel;
        }
    }

    auto& rules = xdg_positioner->rules;
    log_error("xdg_popup<{}> created:", (void*)popup);
    log_error("  size = {}", wrei_to_string(rules.size));
    log_error("  anchor_rect = {}", wrei_to_string(rules.anchor_rect));
    log_error("  anchor = {}", magic_enum::enum_name(rules.anchor));
    log_error("  gravity = {}", magic_enum::enum_name(rules.gravity));
    u32 adjustment = rules.constraint_adjustment;
    while (adjustment) {
        u32 lsb = 1u << std::countr_zero(adjustment);
        log_error("  adjustment |= {}", magic_enum::enum_name(xdg_positioner_constraint_adjustment(lsb)));
        adjustment &= ~lsb;
    }
    log_error("  offset = {}", wrei_to_string(rules.offset));
    log_error("  reactive = {}", rules.reactive);
    log_error("  parent_size = {}", wrei_to_string(rules.parent_size));
    log_error("  parent_configure = {}", rules.parent_configure);

    wroc_resource_set_implementation_refcounted(new_resource, &wroc_xdg_popup_impl, popup);
}

static
void wroc_xdg_popup_position(wroc_popup* popup)
{
    auto parent_pos = wroc_surface_get_position(popup->parent->surface.get());
    auto parent_origin = parent_pos + vec2f64(wroc_xdg_surface_get_geometry(popup->parent.get()).origin);

    auto* base = popup->base();

    rect2i32 constraint {
        .origin = -parent_origin,
    };
    // TODO: Use "primary" output of popup
    // for (auto& output : popup->surface->outputs) {
    for (auto& output : popup->surface->server->output_layout->outputs) {
        constraint.extent = output->size;
        break;
    }
    auto geometry = wroc_xdg_positioner_apply(popup->positioner->rules, constraint);

    base->anchor.relative = {};
    base->anchor.position = parent_origin + vec2f64(geometry.origin);

    if (popup->reposition_token) {
        wroc_send(xdg_popup_send_repositioned, popup->resource, *popup->reposition_token);
        popup->reposition_token = std::nullopt;
    }

    wroc_send(xdg_popup_send_configure, popup->resource, geometry.origin.x, geometry.origin.y, geometry.extent.x, geometry.extent.y);
    wroc_xdg_surface_flush_configure(base);
}

void wroc_popup::on_commit(wroc_surface_commit_flags)
{
    if (!initial_configure_complete) {
        initial_configure_complete = true;

        if (parent) {
            wroc_xdg_popup_position(this);
        } else {
            log_error("xdg_popup has no parent set at commit time, can't configure");
        }

        wroc_surface_raise(surface.get());
    }
}

static
void wroc_xdg_popup_reposition(wl_client* client, wl_resource* resource, wl_resource* _positioner, u32 token)
{
    log_warn("xdg_popup::reposition(token = {})", token);

    auto* popup = wroc_get_userdata<wroc_popup>(resource);
    auto* positioner = wroc_get_userdata<wroc_positioner>(_positioner);

    popup->positioner = positioner;
    popup->reposition_token = token;

    wroc_xdg_popup_position(popup);

    // TODO: We should double buffer the new geometry until the client has committed in response to the new configure
}

const struct xdg_popup_interface wroc_xdg_popup_impl = {
    .destroy    = wroc_surface_addon_destroy,
    WROC_STUB(grab),
    .reposition = wroc_xdg_popup_reposition,
};
