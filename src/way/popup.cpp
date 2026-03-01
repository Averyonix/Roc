#include "internal.hpp"

struct way_axis_region
{
    i32 pos;
    i32 size;
};

struct way_axis_overlaps
{
    i32 start;
    i32 end;
};

struct way_positioner_rules
{
    vec2i32 size;
    rect2i32 anchor_rect;
    xdg_positioner_anchor anchor;
    xdg_positioner_gravity gravity;
    flags<xdg_positioner_constraint_adjustment> constraint_adjustment;
    vec2i32 offset;
    bool reactive = false;
    vec2i32 parent_size;
    u32 parent_configure;
};

struct way_positioner : core_object
{
    way_server* server;

    way_resource resource;

    way_positioner_rules rules;
};

CORE_OBJECT_EXPLICIT_DEFINE(way_positioner);

void way_create_positioner(wl_client* client, wl_resource* resource, u32 id)
{
    auto positioner = core_create<way_positioner>();
    positioner->resource = way_resource_create_refcounted(xdg_positioner, client, resource, id, positioner.get());
}

#define POSITIONER_SET(Name, Expr, ...) \
    .set_##Name = [](wl_client* client, wl_resource* resource __VA_OPT__(,) __VA_ARGS__) { \
        way_get_userdata<way_positioner>(resource)->rules.Name = (Expr); \
    }

WAY_INTERFACE(xdg_positioner) = {
    .destroy = way_simple_destroy,
    POSITIONER_SET(size,                  vec2i32(width, height),                       i32 width, i32 height),
    // POSITIONER_SET(anchor_rect,           rect2i32({x, y}, {width, height}, core_xywh), i32 x, i32 y, i32 width, i32 height),
    .set_anchor_rect = [](wl_client* client, wl_resource* resource, i32 x, i32 y, i32 width, i32 height) {
        auto* positioner = way_get_userdata<way_positioner>(resource);
        positioner->rules.anchor_rect = {{x, y}, {width, height}, core_xywh};
        log_error("ANCHOR_RECT: {}", core_to_string(positioner->rules.anchor_rect));
    },
    POSITIONER_SET(anchor,                xdg_positioner_anchor(anchor),                u32 anchor),
    POSITIONER_SET(gravity,               xdg_positioner_gravity(gravity),              u32 gravity),
    POSITIONER_SET(constraint_adjustment, xdg_positioner_constraint_adjustment(constraint_adjustment), u32 constraint_adjustment),
    POSITIONER_SET(offset,                vec2i32(x, y),                                i32 x, i32 y),
    POSITIONER_SET(reactive,              true),
    POSITIONER_SET(parent_size,           vec2i32(width, height),                       i32 width, i32 height),
    POSITIONER_SET(parent_configure,      serial,                                       u32 serial),
};

#undef POSITIONER_SET

struct way_xdg_positioner_axis_rules
{
    way_axis_region anchor;
    i32 size;
    i32 gravity;
    bool flip;
    bool slide;
    bool resize;
};

#define WAY_NOISY_POSITIONERS 1

static
way_axis_region positioner_apply_axis(const way_xdg_positioner_axis_rules& rules, way_axis_region constraint)
{
#if WAY_NOISY_POSITIONERS
    log_debug("way_xdg_position_apply_axis");
    log_debug("  constraint = ({} ; {})", constraint.pos, constraint.size);
    log_debug("  anchor = ({} ; {})", rules.anchor.pos, rules.anchor.size);
    log_debug("  size, gravity  = {}, {}", rules.size, rules.gravity);
    log_debug("  flip   = {}", rules.flip);
    log_debug("  slide  = {}", rules.slide);
    log_debug("  resize = {}", rules.resize);
#endif

    auto get_position = [](auto& rules) -> way_axis_region {
        return {rules.anchor.pos + rules.gravity - rules.size, rules.size};
    };

    auto get_overlaps = [&](way_axis_region region) -> way_axis_overlaps {
        return {constraint.pos - region.pos, region.pos + region.size - (constraint.pos + constraint.size)};
    };

    auto is_unconstrained = [&](way_axis_region region) {
        auto overlaps = get_overlaps(region);
        return overlaps.start <= 0 && overlaps.end <= 0;
    };

    auto region = get_position(rules);

    if (is_unconstrained(region)) {
        return region;
    }

    static constexpr bool ignore_flip = false;
    static constexpr bool always_slide = false;

    if (rules.flip && !ignore_flip) {
        auto flipped_rules = rules;
        flipped_rules.anchor.pos = rules.anchor.size - rules.anchor.pos;
        flipped_rules.gravity = rules.size - rules.gravity;
        auto flipped = get_position(flipped_rules);
        if (is_unconstrained(flipped)) {
            return flipped;
        }
    }

    if (rules.slide || always_slide) {
        auto overlap = get_overlaps(region);
        if (overlap.start > 0 && overlap.end > 0) {
            // Overlaps both at start and end, move in direction of gravity until opposite edge is unconstrained
            if (rules.gravity == rules.size) {
                region.pos += overlap.start;
            } else if (rules.gravity == 0) {
                region.pos -= overlap.end;
            }
        } else if (overlap.start > 0) {
            // Overlaps at start, move forward
            region.pos += std::min(overlap.start, -overlap.end);
            return region;
        } else if (overlap.end > 0) {
            // Overlaps at end, move backward
            region.pos -= std::min(overlap.end, -overlap.start);
            return region;
        }
    }

    if (rules.resize) {
        auto overlap = get_overlaps(region);
        if (overlap.start > 0 && overlap.end > 0) {
            // Overlaps both, fit constraint exactly
            region = constraint;
        } else if (overlap.start > 0 && overlap.start < region.size) {
            // Overlaps start by less than size, clip start
            region.pos  += overlap.start;
            region.size -= overlap.start;
        } else if (overlap.end > 0 && overlap.end < region.size) {
            // Overlaps end by less than size, clip end
            region.size -= overlap.end;
        }
    }

    // Return best effort

    return region;
}

#define WAY_WAYLAND_EDGES_TO_REL_CASES(Prefix) \
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
core_vec<2, T> positioner_anchor_to_rel(xdg_positioner_anchor anchor, core_vec<2, T> rel)
{
    switch (anchor) {
        WAY_WAYLAND_EDGES_TO_REL_CASES(XDG_POSITIONER_ANCHOR)
    }
}

template<typename T>
core_vec<2, T> positioner_gravity_to_rel(xdg_positioner_gravity gravity, core_vec<2, T> rel)
{
    switch (gravity) {
        WAY_WAYLAND_EDGES_TO_REL_CASES(XDG_POSITIONER_GRAVITY)
    }
}

static
void positioner_apply_axis_from_rules(const way_positioner_rules& rules, rect2i32 constraint, rect2i32& target, u32 axis)
{
    auto anchor = positioner_anchor_to_rel(rules.anchor, rules.anchor_rect.extent);
    auto gravity = positioner_gravity_to_rel(rules.gravity, rules.size);
    static constexpr std::array flip_adjustments   = { XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X,   XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y   };
    static constexpr std::array slide_adjustments  = { XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X,  XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y  };
    static constexpr std::array resize_adjustments = { XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_RESIZE_X, XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_RESIZE_Y };
    way_xdg_positioner_axis_rules axis_rules {
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
    way_axis_region constraint_region {
        .pos = constraint.origin[axis] - offset,
        .size = constraint.extent[axis],
    };
    auto region = positioner_apply_axis(axis_rules, constraint_region);
    target.origin[axis] = region.pos + offset;
    target.extent[axis] = region.size;
}

static
rect2i32 positioner_apply(const way_positioner_rules& rules, rect2i32 constraint)
{
    rect2i32 target;
    positioner_apply_axis_from_rules(rules, constraint, target, 0);
    positioner_apply_axis_from_rules(rules, constraint, target, 1);
    return target;
}

// -----------------------------------------------------------------------------

void way_get_popup(wl_client* client, wl_resource* resource, u32 id, wl_resource* wl_parent, wl_resource* positioner)
{
    auto* surface = way_get_userdata<way_surface>(resource);
    surface->role = way_surface_role::xdg_popup;
    surface->popup.resource = way_resource_create_refcounted(xdg_popup, client, resource, id, surface);

    auto* parent = way_get_userdata<way_surface>(wl_parent);
    surface->parent = parent;

    surface->popup.positioner = way_get_userdata<way_positioner>(positioner);

    log_error("anchor rect 1: {}", core_to_string(surface->popup.positioner->rules.anchor_rect));

    // Place into parent's surface stack
    scene_node_set_transform(surface->scene.transform.get(), parent->scene.transform.get());
    scene_tree_place_above(parent->scene.tree.get(), nullptr, surface->scene.tree.get());

    // Queue initial position operation
    surface->popup.reposition = true;
}

// TODO: This should be shared across this and shell.cpp
static
void configure(way_surface* surface)
{
    auto* server = surface->client->server;
    surface->sent_serial = way_next_serial(server);
    way_send(server, xdg_surface_send_configure, surface->xdg_surface, surface->sent_serial);
}

static
void position(way_surface* surface)
{
    rect2i32 constraint = {{-INT_MAX/4, -INT_MAX/4}, {INT_MAX/2, INT_MAX/2}, core_xywh};

    log_error("anchor rect 2: {}", core_to_string(surface->popup.positioner->rules.anchor_rect));

    auto geometry = positioner_apply(surface->popup.positioner->rules, constraint);
    log_error("popup geometry: {}", core_to_string(geometry));

    auto[o, e] = geometry;
    scene_transform_update(surface->scene.transform.get(), o, 1);
    way_send(surface->client->server, xdg_popup_send_configure, surface->popup.resource, o.x, o.y, e.x, e.y);
    configure(surface);
}

void way_popup_apply(way_surface* surface, way_surface_state& state)
{
    if (surface->popup.reposition) {
        surface->popup.reposition = false;

        position(surface);
    }
}

static
void reposition(wl_client* client, wl_resource* resource, wl_resource* positioner, u32 token)
{
    auto* surface = way_get_userdata<way_surface>(resource);
    surface->popup.positioner = way_get_userdata<way_positioner>(positioner);
    surface->popup.reposition = true;
}

WAY_INTERFACE(xdg_popup) = {
    .destroy = way_simple_destroy,
    WAY_STUB(grab),
    .reposition = reposition,
};
