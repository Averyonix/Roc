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
bool wroc_pointer_resource_matches_focus_client(wroc_pointer* pointer, wl_resource* resource)
{
    if (!pointer->focused_surface) return false;
    if (!pointer->focused_surface->resource) return false;
    return wroc_resource_get_client(resource) == wroc_resource_get_client(pointer->focused_surface->resource);
}

static
void wroc_pointer_update_focus(wroc_pointer* pointer, wroc_surface* focused_surface)
{
    auto* server = pointer->server;
    if (focused_surface != pointer->focused_surface.get()) {
        if (auto* old_surface = pointer->focused_surface.get(); old_surface && old_surface->resource) {
            auto serial = wl_display_next_serial(server->display);
            for (auto* resource : pointer->resources) {
                if (!wroc_pointer_resource_matches_focus_client(pointer, resource)) continue;
                wl_pointer_send_leave(resource, serial, old_surface->resource);
                wroc_pointer_send_frame(resource);
            }
        }
        log_info("Leaving surface: {}", (void*)pointer->focused_surface.get());
        pointer->focused_surface = nullptr;

        if (focused_surface) {
            log_info("Entering surface: {}", (void*)focused_surface);

            pointer->focused_surface = focused_surface;

            auto pos = pointer->layout_position - vec2f64(focused_surface->position);
            auto serial = wl_display_next_serial(pointer->server->display);

            for (auto* resource : pointer->resources) {
                if (!wroc_pointer_resource_matches_focus_client(pointer, resource)) continue;
                wl_pointer_send_enter(resource,
                    serial,
                    focused_surface->resource,
                    wl_fixed_from_double(pos.x),
                    wl_fixed_from_double(pos.y));

                wroc_pointer_send_frame(resource);
            }
        }
    }
}

static
void wroc_pointer_button(wroc_pointer* pointer, u32 button, bool pressed)
{
    if (pointer->server->seat->keyboard && pressed) {
        if (pointer->server->toplevel_under_cursor) {
            log_debug("trying to enter keyboard...");
            wroc_keyboard_enter(pointer->server->seat->keyboard, pointer->server->toplevel_under_cursor->surface.get());
        } else {
            wroc_keyboard_clear_focus(pointer->server->seat->keyboard);
        }
    }

    if (pointer->server->surface_under_cursor) {
        if (pressed && pointer->pressed.size() == 1) {
            log_info("Starting implicit grab");
            pointer->server->implicit_grab_surface = pointer->server->surface_under_cursor;
        }
    }

    auto serial = wl_display_next_serial(pointer->server->display);
    auto time = wroc_get_elapsed_milliseconds(pointer->server);
    for (auto* resources : pointer->resources) {
        if (!wroc_pointer_resource_matches_focus_client(pointer, resources)) continue;
        wl_pointer_send_button(resources,
            serial,
            time,
            button, pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
        wroc_pointer_send_frame(resources);
    }

    if (!pressed && pointer->pressed.empty() && pointer->server->implicit_grab_surface) {
        log_info("Ending implicit grab");
        wroc_data_manager_finish_drag(pointer->server);
        pointer->server->implicit_grab_surface = {};
        wroc_pointer_update_focus(pointer, pointer->server->surface_under_cursor.get());
    }
}

static
void wroc_pointer_motion(wroc_pointer* pointer, wroc_output* output, vec2f64 delta)
{
    // log_trace("pointer({:.3f}, {:.3f})", pos.x, pos.y);

    auto* server = pointer->server;
    auto* focused_surface = server->implicit_grab_surface ? server->implicit_grab_surface.get() : server->surface_under_cursor.get();

    wroc_data_manager_update_drag(server, server->surface_under_cursor.get());

    // log_trace("motion, grab = {}", (void*)server->implicit_grab_surface.surface.get());
    wroc_pointer_update_focus(pointer, focused_surface);

    if (focused_surface && focused_surface->resource) {

        auto time = wroc_get_elapsed_milliseconds(server);
        auto pos = pointer->layout_position - vec2f64(focused_surface->position);

        // log_trace("sending motion to surface: {} ({:.2f}, {:.2f}) [{}]", (void*)focused_surface, pos.x, pos.y, time);

        for (auto* resource : pointer->resources) {
            if (!wroc_pointer_resource_matches_focus_client(pointer, resource)) continue;

            if (wroc_is_client_behind(wroc_resource_get_client(resource))) {
                log_warn("[{}], client is running behind, skipping motion event...", time);
                continue;
            }

            wl_pointer_send_motion(resource,
                time,
                wl_fixed_from_double(pos.x),
                wl_fixed_from_double(pos.y));
            wroc_pointer_send_frame(resource);
        }
    }
}

static
void wroc_pointer_axis(wroc_pointer* pointer, vec2f64 rel)
{
    auto time = wroc_get_elapsed_milliseconds(pointer->server);
    for (auto* resource : pointer->resources) {
        if (!wroc_pointer_resource_matches_focus_client(pointer, resource)) continue;
        if (rel.x) {
            wl_pointer_send_axis(resource,
                time,
                WL_POINTER_AXIS_HORIZONTAL_SCROLL,
                wl_fixed_from_double(rel.x));
        }
        if (rel.y) {
            wl_pointer_send_axis(resource,
                time,
                WL_POINTER_AXIS_VERTICAL_SCROLL,
                wl_fixed_from_double(rel.y));
        }
        wroc_pointer_send_frame(resource);
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
