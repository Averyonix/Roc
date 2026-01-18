#include "event.hpp"

static
bool toplevel_is_interactable(wroc_toplevel* toplevel)
{
    return !toplevel->fullscreen.output;
}

void wroc_begin_move_interaction(wroc_toplevel* toplevel, wroc_seat_pointer* pointer, wroc_directions directions)
{
    if (!toplevel_is_interactable(toplevel)) return;

    server->movesize.grabbed_toplevel = toplevel;
    server->movesize.pointer_grab = pointer->position;
    server->movesize.surface_grab = toplevel->anchor.position;
    server->movesize.relative = {
        directions >= wroc_directions::horizontal ? 1 : 0,
        directions >= wroc_directions::vertical   ? 1 : 0,
    };
    server->interaction_mode = wroc_interaction_mode::move;
}

void wroc_begin_resize_interaction(wroc_toplevel* toplevel, wroc_seat_pointer* pointer, wroc_edges edges)
{
    if (!toplevel_is_interactable(toplevel)) return;

    server->movesize.grabbed_toplevel = toplevel;
    server->movesize.pointer_grab = pointer->position;
    server->movesize.surface_grab = wroc_toplevel_get_layout_rect(toplevel).extent;
    server->interaction_mode = wroc_interaction_mode::size;

    vec2f64 edge_rel = wroc_edges_to_relative(edges);
    vec2f64 delta = edge_rel - toplevel->anchor.relative;

    server->movesize.relative.x = edge_rel.x == 0.5 ? 0 : (delta.x ? 1.0 / delta.x : 0);
    server->movesize.relative.y = edge_rel.y == 0.5 ? 0 : (delta.y ? 1.0 / delta.y : 0);
}

static
void drop_focus()
{
    log_warn("Dropping focus");
    wroc_keyboard_clear_focus(server->seat->keyboard.get());
}

static
void close_window(wroc_toplevel* toplevel)
{
    log_warn("Closing window");
    wroc_toplevel_close(toplevel);
}

bool wroc_handle_movesize_interaction(const wroc_event& base_event)
{
    auto mods = wroc_get_active_modifiers();

    switch (wroc_event_get_type(base_event)) {
        break;case wroc_event_type::keyboard_key: {
            auto& event = static_cast<const wroc_keyboard_event&>(base_event);
            if (event.key.pressed && mods >= wroc_modifiers::mod) {
                if (event.key.upper() == XKB_KEY_S) {
                    drop_focus();
                    return true;
                }
                else if (event.key.upper() == XKB_KEY_Q) {
                    auto* surface = server->seat->keyboard->focused_surface.get();
                    if (auto* toplevel = wroc_surface_get_addon<wroc_toplevel>(surface)) {
                        close_window(toplevel);
                    }
                    return true;
                }
            }
        }
        break;case wroc_event_type::pointer_button: {
            auto& event = static_cast<const wroc_pointer_event&>(base_event);
            if (event.button.pressed) {
                if (event.button.button == BTN_EXTRA) {
                    drop_focus();
                    return true;
                }
                if (mods >= wroc_modifiers::mod) {
                    if (event.button.button == BTN_MIDDLE) {
                        wroc_toplevel* toplevel = nullptr;
                        wroc_get_surface_under_cursor(&toplevel);
                        if (toplevel) {
                            close_window(toplevel);
                        }
                    }

                    wroc_toplevel* toplevel;
                    wroc_get_surface_under_cursor(&toplevel);
                    if (toplevel) {

                        rect2i32 geom = wroc_xdg_surface_get_geometry(toplevel->base());
                        auto cursor_geom_rel = vec2i32(wroc_surface_pos_from_global(toplevel->surface.get(), event.pointer->position));
                        cursor_geom_rel -= geom.origin;
                        auto nine_slice = cursor_geom_rel * 3 / geom.extent;

                        wroc_directions dirs = {};
                        if (nine_slice.x != 1 || nine_slice.y == 1) dirs |= wroc_directions::horizontal;
                        if (nine_slice.y != 1 || nine_slice.x == 1) dirs |= wroc_directions::vertical;

                        if (event.button.button == BTN_LEFT) {
                            if (nine_slice.y == 0) dirs |= wroc_directions::horizontal;
                            wroc_begin_move_interaction(toplevel, event.pointer, dirs);

                        } else if (event.button.button == BTN_RIGHT) {

                            wroc_edges edges = {};
                            if      (nine_slice.x > 1) edges |= wroc_edges::right;
                            else if (nine_slice.x < 1) edges |= wroc_edges::left;
                            if      (nine_slice.y > 1) edges |= wroc_edges::bottom;
                            else if (nine_slice.y < 1) edges |= wroc_edges::top;

                            if (nine_slice.x == 1 && nine_slice.y == 1) {
                                wroc_begin_move_interaction(toplevel, event.pointer, dirs);
                            } else {
                                wroc_toplevel_set_anchor_relative(toplevel, wroc_edges_to_relative(wroc_edges_inverse(edges)));
                                wroc_begin_resize_interaction(toplevel, event.pointer, edges);
                            }
                        }
                    }
                    return true;
                }
            } else if (server->interaction_mode == wroc_interaction_mode::move
                    || server->interaction_mode == wroc_interaction_mode::size) {
                server->interaction_mode = wroc_interaction_mode::normal;
            }
        }
        break;case wroc_event_type::pointer_motion:
            if (server->interaction_mode == wroc_interaction_mode::move ||  server->interaction_mode == wroc_interaction_mode::size) {
                auto& event = static_cast<const wroc_pointer_event&>(base_event);
                auto& movesize = server->movesize;
                if (auto* toplevel = movesize.grabbed_toplevel.get(); toplevel && toplevel_is_interactable(toplevel)) {

                    auto delta = (event.pointer->position - movesize.pointer_grab) * movesize.relative;

                    // TODO: Re-snap to pixel coordinates for appropriate output?

                    if (server->interaction_mode == wroc_interaction_mode::move) {
                        // Move
                        toplevel->anchor.position = movesize.surface_grab + delta;

                    } else if (server->interaction_mode == wroc_interaction_mode::size) {
                        // Resize
                        auto new_size = vec2i32(movesize.surface_grab + delta);

                        new_size = glm::max(new_size, vec2i32{100, 100});

                        wroc_toplevel_set_layout_size(toplevel, new_size);
                        wroc_toplevel_flush_configure(toplevel);
                    }

                    return true;
                } else {
                    server->interaction_mode = wroc_interaction_mode::normal;
                }
            }
        break;default:
            ;
    }

    return false;
}

// -----------------------------------------------------------------------------

static
void focus_cycle(bool reverse, wroc_seat_pointer* pointer)
{
    if (server->surfaces.empty()) return;

    auto in_cycle = [&](wroc_surface* surface) {
        // TODO: Track derived "mapped" state
        return surface->current.buffer && wroc_surface_get_addon<wroc_toplevel>(surface)
            && (!pointer || wroc_surface_point_accepts_input(surface, wroc_surface_pos_from_global(surface, pointer->position)));
    };

    auto iter = std::ranges::find(server->surfaces, server->focus.cycled.get());
    if (iter == server->surfaces.end()) {
        for (auto surface : server->surfaces | std::views::reverse) {
            if (!in_cycle(surface)) continue;
            server->focus.cycled = surface;
            return;
        }
        server->focus.cycled = nullptr;
        return;
    }

    auto orig = iter;

    for (;;) {
        if (reverse) {
            iter++;
            if (iter == server->surfaces.end()) iter = server->surfaces.begin();
        } else {
            if (iter == server->surfaces.begin()) iter = server->surfaces.end();
            iter--;
        }

        if (in_cycle(*iter)) {
            server->focus.cycled = *iter;
            return;
        }

        if (iter == orig) {
            // We wrapped around without finding any surface in cycle
            server->focus.cycled = nullptr;
            return;
        }
    }
}

bool wroc_handle_focus_cycle_interaction(const wroc_event& base_event)
{
    auto mods = wroc_get_active_modifiers();

    switch (wroc_event_get_type(base_event)) {
        break;case wroc_event_type::keyboard_key: {
            auto& event = static_cast<const wroc_keyboard_event&>(base_event);
            if ((event.key.upper() == XKB_KEY_Tab || event.key.upper() == XKB_KEY_ISO_Left_Tab)
                    && event.key.pressed && mods >= wroc_modifiers::mod) {
                if (server->interaction_mode == wroc_interaction_mode::normal) {

                    log_warn("Beginning focus cycle");
                    server->interaction_mode = wroc_interaction_mode::focus_cycle;

                    server->focus.cycled = server->seat->keyboard->focused_surface.get();
                    focus_cycle(mods >= wroc_modifiers::shift, nullptr);

                    return true;
                } else {
                    log_warn("Cyling focus {}", mods >= wroc_modifiers::shift ? "previous" : "next");
                    focus_cycle(mods >= wroc_modifiers::shift, nullptr);

                    return true;
                }
            }
        }
        break;case wroc_event_type::keyboard_modifiers:
            if (!(mods >= wroc_modifiers::mod) && server->interaction_mode == wroc_interaction_mode::focus_cycle) {
                log_warn("Ending focus cycle");
                server->interaction_mode = wroc_interaction_mode::normal;
                if (server->focus.cycled) {
                    wroc_keyboard_enter(server->seat->keyboard.get(), server->focus.cycled.get());
                }
            }
        break;case wroc_event_type::pointer_axis: {
            auto& event = static_cast<const wroc_pointer_event&>(base_event);
            if (event.axis.delta.y && mods >= wroc_modifiers::mod) {
                if (server->interaction_mode == wroc_interaction_mode::normal) {

                    log_warn("Beginning focus cycle under pointer");
                    server->interaction_mode = wroc_interaction_mode::focus_cycle;

                    server->focus.cycled = server->seat->keyboard->focused_surface.get();
                    focus_cycle(mods >= wroc_modifiers::shift, server->seat->pointer.get());
                } else {
                    log_warn("Cycling focus under pointer: {}", event.axis.delta.y);

                    auto reverse = event.axis.delta.y > 0;
                    i32 count = i32(std::abs(event.axis.delta.y));
                    for (i32 i = 0; i < count; ++i) {
                        focus_cycle(reverse, server->seat->pointer.get());
                    }
                }
                return true;
            }
        }
        break;default:
            ;
    }

    return false;
}

// -----------------------------------------------------------------------------

static
rect2i32 get_zone_rect(rect2i32 workarea, vec2i32 zone)
{
    auto& c = server->zone.config;

    rect2i32 out;

    auto get_axis = [&](glm::length_t axis) {
        i32 usable_length = workarea.extent[axis] - (c.padding_inner * (c.zones[axis] - 1));
        f64 ideal_zone_size = f64(usable_length) / c.zones[axis];
        out.origin[axis] = std::round(ideal_zone_size *  zone[axis]     );
        out.extent[axis] = std::round(ideal_zone_size * (zone[axis] + 1)) - out.origin[axis];
        out.origin[axis] += workarea.origin[axis] + c.padding_inner * zone[axis];
    };
    get_axis(0);
    get_axis(1);

    return out;
}

static
void zone_update_regions()
{
    auto& c = server->zone.config;

    // TODO: Scaling

    vec2f64 point = server->seat->pointer->position;

    wroc_output* output;
    wroc_output_layout_clamp_position(server->output_layout.get(), point, &output);

    aabb2i32 workarea = output->layout_rect;
    workarea.min += vec2i32{c.external_padding.left, c.external_padding.top};
    workarea.max -= vec2i32{c.external_padding.right, c.external_padding.bottom};

    aabb2f64 pointer_zone = {};
    bool any_zones = false;

    for (u32 zone_x = 0; zone_x < c.zones.x; ++zone_x) {
        for (u32 zone_y = 0; zone_y < c.zones.y; ++zone_y) {
            aabb2f64 rect = get_zone_rect(workarea, {zone_x, zone_y});
            aabb2f64 check_rect = {
                rect.min - vec2f64(c.selection_leeway),
                rect.max + vec2f64(c.selection_leeway),
                wrei_minmax,
            };
            if (wrei_aabb_contains(check_rect, point)) {
                pointer_zone = any_zones ? wrei_aabb_outer(pointer_zone, rect) : rect;
                any_zones = true;
            }
        }
    }

    if (any_zones) {
        if (server->zone.selecting) {
            server->zone.final_zone = wrei_aabb_outer(server->zone.initial_zone, pointer_zone);
        } else {
            server->zone.final_zone = server->zone.initial_zone = pointer_zone;
        }
    } else {
        server->zone.final_zone = {};
    }
}

bool wroc_handle_zone_interaction(const wroc_event& base_event)
{
    auto mods = wroc_get_active_modifiers();
    auto& event = static_cast<const wroc_pointer_event&>(base_event);
    auto& button = event.button;
    switch (wroc_event_get_type(base_event)) {
        break;case wroc_event_type::pointer_button:
            if (button.button == BTN_LEFT) {
                if (button.pressed && mods >= wroc_modifiers::mod && (!(mods >= wroc_modifiers::shift))) {
                    wroc_toplevel* toplevel;
                    wroc_get_surface_under_cursor(&toplevel);
                    if (toplevel) {
                        server->zone.toplevel = toplevel;
                        if (toplevel_is_interactable(toplevel)) {
                            server->interaction_mode = wroc_interaction_mode::zone;
                            server->zone.selecting = false;
                            zone_update_regions();
                        }
                    }
                    return true;
                } else if (server->interaction_mode == wroc_interaction_mode::zone) {
                    if (server->zone.selecting) {
                        if (auto* toplevel = server->zone.toplevel.get()) {
                            auto r = rect2f64(server->zone.final_zone);
                            wroc_toplevel_set_layout_size(toplevel, r.extent);
                            wroc_toplevel_flush_configure(toplevel);
                            toplevel->anchor.position = r.origin;
                            toplevel->anchor.relative = {};
                            wroc_keyboard_enter(server->seat->keyboard.get(), toplevel->surface.get());
                        }
                    }
                    server->interaction_mode = wroc_interaction_mode::normal;
                    return true;
                }
            } else {
                if (button.button == BTN_RIGHT && server->interaction_mode == wroc_interaction_mode::zone) {
                    if (button.pressed) {
                        server->zone.selecting = !server->zone.selecting;
                        zone_update_regions();
                    }
                    return true;
                }
            }
        break;case wroc_event_type::pointer_motion:
            if (server->interaction_mode == wroc_interaction_mode::zone) {
                zone_update_regions();
                return true;
            }
        break;default:
            ;
    }

    return false;
}
