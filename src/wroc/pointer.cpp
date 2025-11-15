#include "server.hpp"

void wroc_pointer_added(wroc_pointer* pointer)
{
    log_info("POINTER ADDED");

    pointer->server->seat->pointer = pointer;
}

static
void wroc_pointer_send_frame(wl_resource* pointer)
{
    if (wl_resource_get_version(pointer) >= WL_POINTER_FRAME_SINCE_VERSION) {
        wl_pointer_send_frame(pointer);
    }
}

void wroc_pointer_button(wroc_pointer* pointer, u32 button, bool pressed)
{
    if (pointer->server->seat->keyboard) {
        auto mods = wroc_get_active_modifiers(pointer->server);
        if (mods >= wroc_modifiers::mod && pressed && button == BTN_LEFT) {
            log_error("MOD is down while pressing left mouse button");
            auto* toplevel = pointer->server->toplevel_under_cursor.get();
            if (toplevel) {
                log_info("  toplevel_under_cursor = <{}> \"{}\"", toplevel->current.app_id, toplevel->current.title);
                log_info("  start drag");

                auto* server = pointer->server;
                server->movesize.grabbed_toplevel = wrei_weak_from(toplevel);
                server->movesize.pointer_grab = pointer->layout_position;
                server->movesize.surface_grab = toplevel->base->position;
                server->interaction_mode = wroc_interaction_mode::move;
                return;
            }
        }  else if (mods >= wroc_modifiers::mod && pressed && button == BTN_RIGHT) {
            if (auto* toplevel = pointer->server->toplevel_under_cursor.get()) {
                auto* server = pointer->server;
                server->movesize.grabbed_toplevel = wrei_weak_from(toplevel);
                server->movesize.pointer_grab = pointer->layout_position;
                server->movesize.surface_grab = wroc_xdg_surface_get_geometry(toplevel->base.get()).extent;
                server->interaction_mode = wroc_interaction_mode::size;
                return;
            }
        } else if (button == BTN_LEFT && !pressed && pointer->server->interaction_mode == wroc_interaction_mode::move) {
            log_warn("Leaving move state");
            pointer->server->interaction_mode = wroc_interaction_mode::normal;
        } else if (button == BTN_RIGHT && !pressed && pointer->server->interaction_mode == wroc_interaction_mode::size) {
            log_warn("Leaving size state");
            pointer->server->interaction_mode = wroc_interaction_mode::normal;
        }
    } else {
        log_warn("no keyboard attached");
    }

    if (pointer->focused) {
        wl_pointer_send_button(pointer->focused,
            wl_display_next_serial(pointer->server->display),
            wroc_get_elapsed_milliseconds(pointer->server),
            button, pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
        wroc_pointer_send_frame(pointer->focused);
    }
}

void wroc_pointer_absolute(wroc_pointer* pointer, wroc_output* output, wrei_vec2f64 pos)
{
    // log_trace("pointer({:.3f}, {:.3f})", pos.x, pos.y);

    pointer->layout_position = pos + output->position;

    if (pointer->server->interaction_mode == wroc_interaction_mode::move) {
        auto& movesize = pointer->server->movesize;
        if (auto* toplevel = pointer->server->movesize.grabbed_toplevel.get()) {
            toplevel->base->position = movesize.surface_grab + (pointer->layout_position - movesize.pointer_grab);
            // log_trace("new surface position = ({}, {})", toplevel->base->position.x, toplevel->base->position.y);
            return;
        } else {
            pointer->server->interaction_mode = wroc_interaction_mode::normal;
        }
    }

    if (pointer->server->interaction_mode == wroc_interaction_mode::size) {
        auto& movesize = pointer->server->movesize;
        if (auto* toplevel = pointer->server->movesize.grabbed_toplevel.get()) {
            auto new_size = glm::max(movesize.surface_grab + (pointer->layout_position - movesize.pointer_grab), wrei_vec2f64{});
            // log_trace("new surface size = ({}, {}) for toplevel", new_size.x, new_size.y, toplevel->current.title);
            wroc_xdg_toplevel_set_size(toplevel, new_size);
            wroc_xdg_toplevel_flush_configure(toplevel);
        } else {
            pointer->server->interaction_mode = wroc_interaction_mode::normal;
        }
    }

    if (!pointer->focused) {
        // log_trace("wl_pointer.size() = {}", pointer->wl_pointer.size());
        // log_trace("surfaces count = {}", pointer->server->surfaces.size());
        if (pointer->wl_pointers.front() && !pointer->server->surfaces.empty()) {
            pointer->focused = pointer->wl_pointers.front();
            auto* surface = pointer->server->surfaces.front();
            pointer->focused_surface = wrei_weak_from(surface);
            log_debug("entering surface: {}", (void*)surface);
            wl_pointer_send_enter(pointer->focused,
                wl_display_next_serial(pointer->server->display),
                surface->wl_surface,
                wl_fixed_from_double(pos.x),
                wl_fixed_from_double(pos.y));
        }
    }

    if (pointer->focused) {
        if (auto* surface = wroc_xdg_surface::try_from(pointer->focused_surface.get())) {
            // log_trace("sending motion ({:.3f}, {:.3f}) - ({}, {})", pos.x, pos.y, surface->position.x, surface->position.y);
            wl_pointer_send_motion(pointer->focused,
                wroc_get_elapsed_milliseconds(pointer->server),
                wl_fixed_from_double(pos.x - surface->position.x),
                wl_fixed_from_double(pos.y - surface->position.y));
            wroc_pointer_send_frame(pointer->focused);
        }
    }
}

void wroc_pointer_relative(wroc_pointer* pointer, wrei_vec2f64 rel)
{

}

void wroc_pointer_axis(wroc_pointer* pointer, wrei_vec2f64 rel)
{
    if (pointer->focused) {
        if (rel.x) {
            wl_pointer_send_axis(pointer->focused,
                wroc_get_elapsed_milliseconds(pointer->server),
                WL_POINTER_AXIS_HORIZONTAL_SCROLL,
                wl_fixed_from_double(rel.x));
        }
        if (rel.y) {
            wl_pointer_send_axis(pointer->focused,
                wroc_get_elapsed_milliseconds(pointer->server),
                WL_POINTER_AXIS_VERTICAL_SCROLL,
                wl_fixed_from_double(rel.y));
        }
        wroc_pointer_send_frame(pointer->focused);
    }
}
