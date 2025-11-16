#include "server.hpp"

#include "wroc/event.hpp"

static
void wroc_pointer_added(wroc_pointer* pointer)
{
    pointer->server->seat->pointer = pointer;
}

static
void wroc_pointer_send_frame(wl_resource* pointer)
{
    if (wl_resource_get_version(pointer) >= WL_POINTER_FRAME_SINCE_VERSION) {
        wl_pointer_send_frame(pointer);
    }
}

static
void wroc_pointer_button(wroc_pointer* pointer, u32 button, bool pressed)
{
    if (pointer->server->seat->keyboard) {
        if (pointer->server->toplevel_under_cursor.get()) {
            log_debug("trying to enter keyboard...");
            wroc_keyboard_enter(pointer->server->seat->keyboard, pointer->server->toplevel_under_cursor.get()->base->surface.get());
        } else {
            wroc_keyboard_clear_focus(pointer->server->seat->keyboard);
        }
    }

    if (pointer->focused) {
        wl_pointer_send_button(pointer->focused,
            wl_display_next_serial(pointer->server->display),
            wroc_get_elapsed_milliseconds(pointer->server),
            button, pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
        wroc_pointer_send_frame(pointer->focused);
    }
}

static
void wroc_pointer_motion(wroc_pointer* pointer, wroc_output* output, vec2f64 delta)
{
    // log_trace("pointer({:.3f}, {:.3f})", pos.x, pos.y);

    auto pos = pointer->layout_position;

    auto* server = pointer->server;
    auto* under_cursor = server->toplevel_under_cursor.get();
    auto* surface_under_cursor = under_cursor ? under_cursor->base->surface.get() : nullptr;
    if (surface_under_cursor != pointer->focused_surface.get()) {
        if (auto* old_surface = pointer->focused_surface.get(); old_surface && old_surface->wl_surface) {
            wl_pointer_send_leave(pointer->focused, wl_display_next_serial(server->display), old_surface->wl_surface);
            wroc_pointer_send_frame(pointer->focused);
        }
        log_info("Leaving surface: {}", (void*)pointer->focused_surface.get());
        pointer->focused_surface = nullptr;
        pointer->focused = nullptr;

        if (surface_under_cursor) {
            log_info("Entering surface: {}", (void*)surface_under_cursor);
            auto* client = wl_resource_get_client(surface_under_cursor->wl_surface);
            for (auto* p : pointer->wl_pointers) {
                if (wl_resource_get_client(p) != client) continue;

                pointer->focused = p;
                pointer->focused_surface = wrei_weak_from(surface_under_cursor);

                wl_pointer_send_enter(pointer->focused,
                    wl_display_next_serial(pointer->server->display),
                    surface_under_cursor->wl_surface,
                    wl_fixed_from_double(pos.x),
                    wl_fixed_from_double(pos.y));

                wroc_pointer_send_frame(pointer->focused);

                break;
            }
        }
    } else if (auto* xdg_surface = wroc_xdg_surface::try_from(pointer->focused_surface.get());
            pointer->focused && xdg_surface && xdg_surface->surface->wl_surface) {
        // log_trace("sending motion to surface: {}", (void*)surface);
        auto geom = wroc_xdg_surface_get_geometry(xdg_surface);
        wl_pointer_send_motion(pointer->focused,
            wroc_get_elapsed_milliseconds(server),
            wl_fixed_from_double(pos.x - xdg_surface->position.x + geom.origin.x),
            wl_fixed_from_double(pos.y - xdg_surface->position.y + geom.origin.y));
        wroc_pointer_send_frame(pointer->focused);
    }
}

static
void wroc_pointer_axis(wroc_pointer* pointer, vec2f64 rel)
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

void wroc_handle_pointer_event(wroc_server* server, const wroc_pointer_event& event)
{
    switch (event.type) {
        case wroc_event_type::pointer_added:
            wroc_pointer_added(event.pointer);
            break;
        case wroc_event_type::pointer_button:
            wroc_pointer_button(event.pointer, event.button.button, event.button.pressed);
            break;
        case wroc_event_type::pointer_motion:
            wroc_pointer_motion(event.pointer, event.output, event.motion.delta);
            break;
        case wroc_event_type::pointer_axis:
            wroc_pointer_axis(event.pointer, event.axis.delta);
            break;
        default:
            break;
    }
}
