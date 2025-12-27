#include "event.hpp"

static
bool toplevel_is_interactable(wroc_toplevel* toplevel)
{
    return !toplevel->fullscreen.output;
}

void wroc_begin_move_interaction(wroc_toplevel* toplevel, wroc_seat_pointer* pointer, wroc_directions directions)
{
    if (!toplevel_is_interactable(toplevel)) return;

    auto* server = toplevel->surface->server;
    server->movesize.grabbed_toplevel = toplevel;
    server->movesize.pointer_grab = pointer->position;
    server->movesize.surface_grab = toplevel->anchor.position;
    server->movesize.directions = directions;
    server->interaction_mode = wroc_interaction_mode::move;
}

void wroc_begin_resize_interaction(wroc_toplevel* toplevel, wroc_seat_pointer* pointer, vec2i32 new_anchor_rel, wroc_directions directions)
{
    if (!toplevel_is_interactable(toplevel)) return;

    auto* server = toplevel->surface->server;
    server->movesize.grabbed_toplevel = toplevel;
    server->movesize.pointer_grab = pointer->position;
    server->movesize.surface_grab = wroc_toplevel_get_layout_rect(toplevel).extent;
    server->movesize.directions = directions;
    server->interaction_mode = wroc_interaction_mode::size;

    // Update surface anchor position
    // TODO: Should this be a function on wroc_toplevel itself?

    toplevel->anchor.position += vec2f64(new_anchor_rel - toplevel->anchor.relative) * server->movesize.surface_grab;
    toplevel->anchor.relative = new_anchor_rel;
}

bool wroc_handle_movesize_interaction(wroc_server* server, const wroc_event& base_event)
{
    if (wroc_event_get_type(base_event) == wroc_event_type::pointer_button) {
        auto& event = static_cast<const wroc_pointer_event&>(base_event);
        if (event.button.pressed) {
            if (wroc_get_active_modifiers(server) >= wroc_modifiers::mod) {

                wroc_toplevel* toplevel;
                wroc_get_surface_under_cursor(server, &toplevel);
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

                        vec2i32 anchor_rel = toplevel->anchor.relative;
                        if      (nine_slice.x > 1) anchor_rel.x = 0;
                        else if (nine_slice.x < 1) anchor_rel.x = 1;
                        if      (nine_slice.y > 1) anchor_rel.y = 0;
                        else if (nine_slice.y < 1) anchor_rel.y = 1;

                        if (nine_slice.x == 1 && nine_slice.y == 1) {
                            wroc_begin_move_interaction(toplevel, event.pointer, dirs);
                        } else {
                            wroc_begin_resize_interaction(toplevel, event.pointer, anchor_rel, dirs);
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

    if (wroc_event_get_type(base_event) == wroc_event_type::pointer_motion
            && (server->interaction_mode == wroc_interaction_mode::move
            ||  server->interaction_mode == wroc_interaction_mode::size)) {
        auto& event = static_cast<const wroc_pointer_event&>(base_event);
        auto& movesize = server->movesize;
        if (auto* toplevel = movesize.grabbed_toplevel.get(); toplevel && toplevel_is_interactable(toplevel)) {

            auto delta = event.pointer->position - movesize.pointer_grab;
            if (!(movesize.directions >= wroc_directions::horizontal)) delta.x = 0;
            if (!(movesize.directions >= wroc_directions::vertical))   delta.y = 0;

            // TODO: Re-snap to pixel coordinates for appropriate output?

            if (server->interaction_mode == wroc_interaction_mode::move) {
                // Move
                toplevel->anchor.position = movesize.surface_grab + delta;

            } else if (server->interaction_mode == wroc_interaction_mode::size) {
                // Resize
                auto new_size = vec2i32(movesize.surface_grab) + (vec2i32(delta) * (vec2i32(1) - toplevel->anchor.relative * vec2i32(2)));

                new_size = glm::max(new_size, vec2i32{100, 100});

                wroc_toplevel_set_layout_size(toplevel, new_size);
                wroc_toplevel_flush_configure(toplevel);
            }

            return true;
        } else {
            server->interaction_mode = wroc_interaction_mode::normal;
        }
    }

    return false;
}
