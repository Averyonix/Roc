#include "event.hpp"

bool wroc_handle_movesize_interaction(wroc_server* server, const wroc_event& base_event)
{
    if (base_event.type == wroc_event_type::pointer_button) {
        auto& event = static_cast<const wroc_pointer_event&>(base_event);
        if (event.button.pressed) {
            if (wroc_get_active_modifiers(server) >= wroc_modifiers::mod) {
                auto* toplevel = server->toplevel_under_cursor.get();
                if (toplevel) {
                    server->movesize.grabbed_toplevel = wrei_weak_from(toplevel);
                    server->movesize.pointer_grab = event.pointer->layout_position;
                    if (event.button.button == BTN_LEFT) {
                        server->movesize.surface_grab = toplevel->base->position;
                        server->interaction_mode = wroc_interaction_mode::move;
                    }
                    else if (event.button.button == BTN_RIGHT) {
                        server->movesize.surface_grab = wroc_xdg_surface_get_geometry(toplevel->base.get()).extent;
                        server->interaction_mode = wroc_interaction_mode::size;
                    }
                }
                return true;
            }
        } else if (server->interaction_mode == wroc_interaction_mode::move
                || server->interaction_mode == wroc_interaction_mode::size) {
            server->interaction_mode = wroc_interaction_mode::normal;
        }
    }

    if (base_event.type == wroc_event_type::pointer_motion
            && (server->interaction_mode == wroc_interaction_mode::move
            ||  server->interaction_mode == wroc_interaction_mode::size)) {
        auto& event = static_cast<const wroc_pointer_event&>(base_event);
        auto& movesize = server->movesize;
        if (auto* toplevel = movesize.grabbed_toplevel.get()) {
            if (server->interaction_mode == wroc_interaction_mode::move) {
                toplevel->base->position = movesize.surface_grab + wrei_vec2i32(glm::round(event.pointer->layout_position - movesize.pointer_grab));
            } else if (server->interaction_mode == wroc_interaction_mode::size) {
                auto new_size = glm::max(movesize.surface_grab + wrei_vec2i32(glm::round(event.pointer->layout_position - movesize.pointer_grab)), wrei_vec2i32{});
                wroc_xdg_toplevel_set_size(toplevel, new_size);
                wroc_xdg_toplevel_flush_configure(toplevel);
            }
        } else {
            server->interaction_mode = wroc_interaction_mode::normal;
        }
    }

    return false;
}
