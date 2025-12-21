#include "server.hpp"

#include "wroc/event.hpp"

static void wroc_seat_pointer_update_state(wroc_seat_pointer*, wroc_key_action, std::span<const u32> actioned_buttons);

void wroc_pointer::press(u32 keycode)
{
    if (!pressed.insert(keycode).second) return;

    wroc_seat_pointer_update_state(target.get(), wroc_key_action::press, {keycode});
}

void wroc_pointer::release(u32 keycode)
{
    if (!pressed.erase(keycode)) return;

    wroc_seat_pointer_update_state(target.get(), wroc_key_action::release, {keycode});
}

void wroc_pointer::enter(std::span<const u32> keycodes)
{
    std::vector<u32> filtered;

    for (auto& keycode : keycodes) {
        if (pressed.insert(keycode).second) {
            filtered.emplace_back(keycode);
        }
    }

    if (filtered.empty()) return;

    wroc_seat_pointer_update_state(target.get(), wroc_key_action::enter, filtered);
}

void wroc_pointer::leave()
{
    wroc_seat_pointer_update_state(target.get(), wroc_key_action::release, pressed);

    pressed.clear();
}

void wroc_pointer::absolute(vec2f64 layout_position)
{
    target->layout_position = layout_position;

    wroc_post_event(server, wroc_pointer_event {
        .type = wroc_event_type::pointer_motion,
        .pointer = target.get(),
        .motion = {},
    });
}

void wroc_pointer::relative(vec2f64 delta)
{
    // TODO: Output layouts
    auto* output = server->outputs.front();
    auto pos = target->layout_position + delta;
    if (pos.x < 0) pos.x = 0;
    if (pos.y < 0) pos.y = 0;
    if (pos.x >= output->size.x) pos.x = output->size.x - 1;
    if (pos.y >= output->size.y) pos.y = output->size.y - 1;

    target->layout_position = pos;

    // TODO: Separate absolute and relative motion events?
    wroc_post_event(server, wroc_pointer_event {
        .type = wroc_event_type::pointer_motion,
        .pointer = target.get(),
        .motion = {},
    });
}

void wroc_pointer::scroll(vec2f64 delta)
{
    // TODO: Separate axis and scroll events
    wroc_post_event(server, wroc_pointer_event {
        .type = wroc_event_type::pointer_axis,
        .pointer = target.get(),
        .axis {
            .delta = delta,
        },
    });
}

wroc_pointer::~wroc_pointer()
{
    leave();

    if (target) {
        std::erase(target->sources, this);
    }
}

void wroc_seat_pointer::attach(wroc_pointer* kb)
{
    assert(!kb->target && "wroc_pointer already attached to seat pointer");

    sources.emplace_back(kb);
    kb->target = this;

    wroc_seat_pointer_update_state(this, wroc_key_action::enter, kb->pressed);
}

void wroc_seat_init_pointer(wroc_seat* seat)
{
    seat->pointer = wrei_create<wroc_seat_pointer>();
    seat->pointer->seat = seat;
}

static
void wroc_seat_pointer_update_state(wroc_seat_pointer* pointer, wroc_key_action action, std::span<const u32> actioned_buttons)
{
    for (auto button : actioned_buttons) {
        if (action == wroc_key_action::release ? pointer->pressed.dec(button) : pointer->pressed.inc(button)) {
            log_trace("button {} - {}", libevdev_event_code_get_name(EV_KEY, button), magic_enum::enum_name(action));
            if (action != wroc_key_action::enter) {
                wroc_post_event(pointer->seat->server, wroc_pointer_event {
                    .type = wroc_event_type::pointer_button,
                    .pointer = pointer,
                    .button { .button = button, .pressed = action == wroc_key_action::press },
                });
            }
        }
    }
}

static
void wroc_pointer_send_frame(wl_resource* pointer)
{
    if (wl_resource_get_version(pointer) >= WL_POINTER_FRAME_SINCE_VERSION) {
        wl_pointer_send_frame(pointer);
    }
}

static
bool wroc_pointer_resource_matches_focus_client(wroc_seat_pointer* pointer, wl_resource* resource)
{
    if (!pointer->focused_surface) return false;
    if (!pointer->focused_surface->resource) return false;
    return wroc_resource_get_client(resource) == wroc_resource_get_client(pointer->focused_surface->resource);
}

static
void wroc_pointer_update_focus(wroc_seat_pointer* pointer, wroc_surface* focused_surface)
{
    auto* server = pointer->seat->server;
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
            auto serial = wl_display_next_serial(pointer->seat->server->display);

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
void wroc_pointer_button(wroc_seat_pointer* pointer, u32 button, bool pressed)
{
    auto* seat = pointer->seat;
    auto* server = seat->server;

    if (seat->keyboard && pressed) {
        if (server->toplevel_under_cursor) {
            log_debug("trying to enter keyboard...");
            wroc_keyboard_enter(seat->keyboard.get(), server->toplevel_under_cursor->surface.get());
        } else {
            wroc_keyboard_clear_focus(seat->keyboard.get());
        }
    }

    if (server->surface_under_cursor) {
        if (pressed && pointer->pressed.size() == 1) {
            log_info("Starting implicit grab");
            server->implicit_grab_surface = server->surface_under_cursor;
        }
    }

    auto serial = wl_display_next_serial(server->display);
    auto time = wroc_get_elapsed_milliseconds(server);
    for (auto* resources : pointer->resources) {
        if (!wroc_pointer_resource_matches_focus_client(pointer, resources)) continue;
        wl_pointer_send_button(resources,
            serial,
            time,
            button, pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
        wroc_pointer_send_frame(resources);
    }

    if (!pressed && pointer->pressed.empty() && server->implicit_grab_surface) {
        log_info("Ending implicit grab");
        wroc_data_manager_finish_drag(server);
        server->implicit_grab_surface = {};
        wroc_pointer_update_focus(pointer, server->surface_under_cursor.get());
    }
}

static
void wroc_pointer_motion(wroc_seat_pointer* pointer, vec2f64 delta)
{
    // log_trace("pointer({:.3f}, {:.3f})", pos.x, pos.y);

    auto* server = pointer->seat->server;
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
void wroc_pointer_axis(wroc_seat_pointer* pointer, vec2f64 rel)
{
    // TODO: Handle different types of scroll correctly

    auto time = wroc_get_elapsed_milliseconds(pointer->seat->server);
    for (auto* resource : pointer->resources) {
        if (!wroc_pointer_resource_matches_focus_client(pointer, resource)) continue;

        auto version = wl_resource_get_version(resource);
        if (version >= WL_POINTER_AXIS_VALUE120_SINCE_VERSION) {
            if (rel.x) wl_pointer_send_axis_value120(resource, WL_POINTER_AXIS_HORIZONTAL_SCROLL, i32(rel.x * 120));
            if (rel.y) wl_pointer_send_axis_value120(resource, WL_POINTER_AXIS_VERTICAL_SCROLL,   i32(rel.y * 120));
        } else if (version >= WL_POINTER_AXIS_DISCRETE_SINCE_VERSION) {
            // TODO: Accumulate fractional values
            if (rel.x) wl_pointer_send_axis_discrete(resource, WL_POINTER_AXIS_HORIZONTAL_SCROLL, i32(rel.x));
            if (rel.y) wl_pointer_send_axis_discrete(resource, WL_POINTER_AXIS_VERTICAL_SCROLL,   i32(rel.y));
        } else {
            constexpr double discrete_to_pixels = 15;
            if (rel.x) wl_pointer_send_axis(resource, time, WL_POINTER_AXIS_HORIZONTAL_SCROLL, wl_fixed_from_double(rel.x * discrete_to_pixels));
            if (rel.y) wl_pointer_send_axis(resource, time, WL_POINTER_AXIS_VERTICAL_SCROLL,   wl_fixed_from_double(rel.y * discrete_to_pixels));
        }

        if (version >= WL_POINTER_FRAME_SINCE_VERSION) {
            wroc_pointer_send_frame(resource);
        }
    }
}

void wroc_handle_pointer_event(wroc_server* server, const wroc_pointer_event& event)
{
    switch (event.type) {
        break;case wroc_event_type::pointer_button:
            wroc_pointer_button(event.pointer, event.button.button, event.button.pressed);
        break;case wroc_event_type::pointer_motion:
            wroc_pointer_motion(event.pointer, event.motion.delta);
        break;case wroc_event_type::pointer_axis:
            wroc_pointer_axis(event.pointer, event.axis.delta);
        break;default:
    }
}
