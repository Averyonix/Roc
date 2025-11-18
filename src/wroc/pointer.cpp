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
    return wl_resource_get_client(resource) == wl_resource_get_client(pointer->focused_surface->resource);
}

static
void wroc_pointer_button(wroc_pointer* pointer, u32 button, bool pressed)
{
    if (pointer->server->seat->keyboard) {
        if (pointer->server->toplevel_under_cursor) {
            log_debug("trying to enter keyboard...");
            wroc_keyboard_enter(pointer->server->seat->keyboard, pointer->server->toplevel_under_cursor.surface.get());
        } else {
            wroc_keyboard_clear_focus(pointer->server->seat->keyboard);
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
}

static
void wroc_pointer_motion(wroc_pointer* pointer, wroc_output* output, vec2f64 delta)
{
    // log_trace("pointer({:.3f}, {:.3f})", pos.x, pos.y);

    auto* server = pointer->server;
    auto& surface_under_cursor = server->surface_under_cursor;
    if (surface_under_cursor.surface.get() != pointer->focused_surface.get()) {
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

        if (surface_under_cursor) {
            log_info("Entering surface: {}", (void*)surface_under_cursor.surface.get());
            auto* client = wl_resource_get_client(surface_under_cursor.surface->resource);
            for (auto* p : pointer->resources) {
                if (wl_resource_get_client(p) != client) continue;

                pointer->focused_surface = wrei_weak_from(surface_under_cursor.surface.get());

                auto pos = pointer->layout_position - vec2f64(surface_under_cursor.position);

                auto serial = wl_display_next_serial(pointer->server->display);

                for (auto* resource : pointer->resources) {
                    if (!wroc_pointer_resource_matches_focus_client(pointer, resource)) continue;
                    wl_pointer_send_enter(resource,
                        serial,
                        surface_under_cursor.surface->resource,
                        wl_fixed_from_double(pos.x),
                        wl_fixed_from_double(pos.y));

                    wroc_pointer_send_frame(resource);
                }

                break;
            }
        }
    } else if (surface_under_cursor && surface_under_cursor.surface->resource) {
        // log_trace("sending motion to surface: {}", (void*)surface);

        auto time = wroc_get_elapsed_milliseconds(server);
        auto pos = pointer->layout_position - vec2f64(surface_under_cursor.position);

        for (auto* resource : pointer->resources) {
            if (!wroc_pointer_resource_matches_focus_client(pointer, resource)) continue;

            if (wroc_is_client_behind(wl_resource_get_client(resource))) {
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
