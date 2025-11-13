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
    if (pointer->focused) {
        wl_pointer_send_button(pointer->focused,
            wl_display_next_serial(pointer->server->display),
            wroc_server_get_elapsed_milliseconds(pointer->server),
            button, pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
        wroc_pointer_send_frame(pointer->focused);
    }
}

void wroc_pointer_absolute(wroc_pointer* pointer, wroc_output* output, wrei_vec2f64 pos)
{
    // log_trace("pointer({:.3f}, {:.3f})", pos.x, pos.y);

    if (!pointer->focused) {
        // log_trace("wl_pointer.size() = {}", pointer->wl_pointer.size());
        // log_trace("surfaces count = {}", pointer->server->surfaces.size());
        if (!pointer->wl_pointer.empty() && !pointer->server->surfaces.empty()) {
            pointer->focused = pointer->wl_pointer.front();
            auto* surface = pointer->server->surfaces.front();
            log_debug("entering surface");
            wl_pointer_send_enter(pointer->focused,
                wl_display_next_serial(pointer->server->display),
                surface->wl_surface,
                wl_fixed_from_double(pos.x),
                wl_fixed_from_double(pos.y));
        }
    }

    if (pointer->focused) {
        log_trace("sending motion ({:.3f}, {:.3f})", pos.x, pos.y);
        wl_pointer_send_motion(pointer->focused,
            wroc_server_get_elapsed_milliseconds(pointer->server),
            wl_fixed_from_double(pos.x), wl_fixed_from_double(pos.y));
        wroc_pointer_send_frame(pointer->focused);
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
                wroc_server_get_elapsed_milliseconds(pointer->server),
                WL_POINTER_AXIS_HORIZONTAL_SCROLL,
                wl_fixed_from_double(rel.x));
        }
        if (rel.y) {
            wl_pointer_send_axis(pointer->focused,
                wroc_server_get_elapsed_milliseconds(pointer->server),
                WL_POINTER_AXIS_VERTICAL_SCROLL,
                wl_fixed_from_double(rel.y));
        }
        wroc_pointer_send_frame(pointer->focused);
    }
}
