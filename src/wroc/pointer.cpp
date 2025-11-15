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
    if (pointer->focused) {
        wl_pointer_send_button(pointer->focused,
            wl_display_next_serial(pointer->server->display),
            wroc_get_elapsed_milliseconds(pointer->server),
            button, pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
        wroc_pointer_send_frame(pointer->focused);
    }
}

static
void wroc_pointer_absolute(wroc_pointer* pointer, wroc_output* output, wrei_vec2f64 pos)
{
    // log_trace("pointer({:.3f}, {:.3f})", pos.x, pos.y);

    if (!pointer->focused) {
        if (pointer->wl_pointers.front()) {
            wroc_surface* surface = nullptr;
            for (auto* s : pointer->server->surfaces) {
                if (wroc_xdg_surface::try_from(s)) {
                    surface = s;
                }
            }

            if (surface) {
                pointer->focused = pointer->wl_pointers.front();
                // auto* surface = pointer->server->surfaces.front();
                pointer->focused_surface = wrei_weak_from(surface);
                log_debug("entering surface: {}", (void*)surface);
                wl_pointer_send_enter(pointer->focused,
                    wl_display_next_serial(pointer->server->display),
                    surface->wl_surface,
                    wl_fixed_from_double(pos.x),
                    wl_fixed_from_double(pos.y));
            } else {
                log_debug("Failed to find xdg_surface to pair with wl_pointer");
            }
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

static
void wroc_pointer_relative(wroc_pointer* pointer, wrei_vec2f64 rel)
{

}

static
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

void wroc_handle_pointer_event(wroc_server* server, const wroc_pointer_event& event)
{
    switch (event.type) {
        case wroc_event_type::pointer_added:
            wroc_pointer_added(event.pointer);
            break;
        case wroc_event_type::pointer_button:
            wroc_pointer_button(event.pointer, event.button.button, event.button.pressed);
            break;
        case wroc_event_type::pointer_absolute:
            wroc_pointer_absolute(event.pointer, event.output, event.absolute.position);
            break;
        case wroc_event_type::pointer_relative:
            wroc_pointer_relative(event.pointer, event.relative.delta);
            break;
        case wroc_event_type::pointer_axis:
            wroc_pointer_axis(event.pointer, event.relative.delta);
            break;
        default:
            break;
    }
}
