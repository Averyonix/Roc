#include "protocol.hpp"

#include "wroc/event.hpp"

const u32 wroc_zwp_relative_pointer_manager_v1_version = 1;

void wroc_zwp_relative_pointer_manager_v1_bind_global(wl_client* client, void* data, u32 version, u32 id)
{
    auto new_resource = wroc_resource_create(client, &zwp_relative_pointer_manager_v1_interface, version, id);
    wroc_resource_set_implementation(new_resource, &wroc_zwp_relative_pointer_manager_v1_impl, nullptr);

    log_error("RELATIVE POINTER MANAGER CREATED");
}

static
void get_relative_pointer(wl_client* client, wl_resource* resource, uint32_t id, wl_resource* _pointer)
{
    auto new_resource = wroc_resource_create(client, &zwp_relative_pointer_v1_interface, wl_resource_get_version(resource), id);
    auto* pointer = wroc_get_userdata<wroc_seat_pointer>(_pointer);
    pointer->relative_pointers.emplace_back(new_resource);
    wroc_resource_set_implementation(new_resource, &wroc_zwp_relative_pointer_v1_impl, pointer);

    log_error("RELATIVE POINTER CREATED");
}

const struct zwp_relative_pointer_manager_v1_interface wroc_zwp_relative_pointer_manager_v1_impl {
    .destroy = wroc_simple_resource_destroy_callback,
    .get_relative_pointer = get_relative_pointer,
};

const struct zwp_relative_pointer_v1_interface wroc_zwp_relative_pointer_v1_impl {
    .destroy = wroc_simple_resource_destroy_callback,
};

// -----------------------------------------------------------------------------

const u32 wroc_zwp_pointer_constraints_v1_version = 1;

void wroc_zwp_pointer_constraints_v1_bind_global(wl_client* client, void* data, u32 version, u32 id)
{
    auto* new_resource = wroc_resource_create(client, &zwp_pointer_constraints_v1_interface, version, id);
    wroc_resource_set_implementation(new_resource, &wroc_zwp_pointer_constraints_v1_impl, nullptr);

    log_error("POINTER CONSTRAINT INTERFACE BOUND");
}

static
void constrain_pointer(
    wroc_pointer_constraint_type type,
    wl_client* client,
    wl_resource* resource,
    u32 id,
    wl_resource* _surface,
    wl_resource* _pointer,
    wl_resource* _region,
    u32 lifetime)
{
    log_error("POINTER CONSTRAINT CREATED: type = {}, lifetime = {}",
        wrei_enum_to_string(type),
        wrei_enum_to_string(zwp_pointer_constraints_v1_lifetime(lifetime)));

    auto* surface = wroc_get_userdata<wroc_surface>(_surface);
    auto* pointer = wroc_get_userdata<wroc_seat_pointer>(_pointer);

    const wl_interface* interface;
    const void* implementation;
    switch (type) {
        break;case wroc_pointer_constraint_type::locked:
            interface = &zwp_locked_pointer_v1_interface;
            implementation = &wroc_zwp_locked_pointer_v1_impl;
        break;case wroc_pointer_constraint_type::confined:
            interface = &zwp_confined_pointer_v1_interface;
            implementation = &wroc_zwp_confined_pointer_v1_impl;
    }

    auto* new_resource = wroc_resource_create(client, interface, wl_resource_get_version(resource), id);
    auto* constraint = wrei_create_unsafe<wroc_pointer_constraint>();
    wroc_resource_set_implementation_refcounted(new_resource, implementation, constraint);
    constraint->type = type;
    constraint->pointer = pointer;
    constraint->resource = new_resource;
    constraint->lifetime = zwp_pointer_constraints_v1_lifetime(lifetime);
    wroc_surface_put_addon(surface, constraint);

    if (_region) {
        // TODO: Do we apply this immediately, or add it to pending as with set_region requests?
        auto* region = wroc_get_userdata<wroc_region>(_region);
        constraint->current.region = region->region;
        constraint->current.committed |= wroc_pointer_constraint_committed_state::region;
    }
}

static
void lock_pointer(auto ...args)
{
    constrain_pointer(wroc_pointer_constraint_type::locked, args...);
}

static
void confine_pointer(auto ...args)
{
    constrain_pointer(wroc_pointer_constraint_type::confined, args...);
}

void wroc_pointer_constraint::on_commit(wroc_surface_commit_flags)
{
    if (pending.committed >= wroc_pointer_constraint_committed_state::region) {
        current.region = std::move(pending.region);
        current.committed |= wroc_pointer_constraint_committed_state::region;
    } else if (pending.committed >= wroc_pointer_constraint_committed_state::region_unset) {
        current.committed -= wroc_pointer_constraint_committed_state::region;
    }

    if (pending.committed >= wroc_pointer_constraint_committed_state::position_hint) {
        current.position_hint = pending.position_hint;
        current.committed |= wroc_pointer_constraint_committed_state::position_hint;
    }

    current.committed |= pending.committed;
    pending = {};
}

void wroc_pointer_constraint::activate()
{
    if (pointer->active_constraint == this) {
        // Already active
        return;
    } else if (pointer->active_constraint) {
        // Replace existing
        pointer->active_constraint->deactivate();
    }

    log_error("POINTER CONSTRAINT {} ACTIVATED", wrei_enum_to_string(type));

    pointer->active_constraint = this;

    switch (type) {
        break;case wroc_pointer_constraint_type::locked:
            wroc_send(zwp_locked_pointer_v1_send_locked, resource);
        break;case wroc_pointer_constraint_type::confined:
            wroc_send(zwp_confined_pointer_v1_send_confined, resource);
    }
}

void wroc_pointer_constraint::deactivate()
{
    if (pointer->active_constraint != this) {
        // Not currently active
        return;
    }

    pointer->active_constraint = nullptr;

    log_error("POINTER CONSTRAINT DEACTIVATED");

    switch (type) {
        break;case wroc_pointer_constraint_type::locked:
            wroc_send(zwp_locked_pointer_v1_send_unlocked, resource);
        break;case wroc_pointer_constraint_type::confined:
            wroc_send(zwp_confined_pointer_v1_send_unconfined, resource);
    }
}

wroc_pointer_constraint::~wroc_pointer_constraint()
{
    log_error("POINTER CONSTRIANT DESTROYED, deactivating");
    if (pointer->active_constraint == this) {
        pointer->active_constraint = nullptr;
    }
}

static
void pointer_constraints_set_region(wl_client* client, wl_resource* resource, wl_resource* _region)
{
    auto* constraint = wroc_get_userdata<wroc_pointer_constraint>(resource);
    if (_region) {
        auto* region = wroc_get_userdata<wroc_region>(_region);
        constraint->pending.region = region->region;
        constraint->pending.committed |= wroc_pointer_constraint_committed_state::region;
        constraint->pending.committed -= wroc_pointer_constraint_committed_state::region_unset;
    } else {
        constraint->pending.committed |= wroc_pointer_constraint_committed_state::region_unset;
        constraint->pending.committed -= wroc_pointer_constraint_committed_state::region;
    }
}

static
void pointer_constraints_set_cursor_position_hint(wl_client* client, wl_resource* resource, wl_fixed_t sx, wl_fixed_t sy)
{
    auto* constraint = wroc_get_userdata<wroc_pointer_constraint>(resource);
    constraint->pending.position_hint = {wl_fixed_to_double(sx), wl_fixed_to_double(sy)};
    constraint->pending.committed |= wroc_pointer_constraint_committed_state::position_hint;

    // log_error("CONSTRAINT SET CURSOR POSITION HINT: {}", wrei_to_string(constraint->pending.position_hint));
}

const struct zwp_pointer_constraints_v1_interface wroc_zwp_pointer_constraints_v1_impl {
    .destroy = wroc_simple_resource_destroy_callback,
    .lock_pointer = lock_pointer,
    .confine_pointer = confine_pointer,
};

const struct zwp_locked_pointer_v1_interface wroc_zwp_locked_pointer_v1_impl {
    .destroy = wroc_simple_resource_destroy_callback,
    .set_cursor_position_hint = pointer_constraints_set_cursor_position_hint,
    .set_region = pointer_constraints_set_region,
};

const struct zwp_confined_pointer_v1_interface wroc_zwp_confined_pointer_v1_impl {
    .destroy = wroc_simple_resource_destroy_callback,
    .set_region = pointer_constraints_set_region,
};

static
bool is_constraint_focused(wroc_pointer_constraint* constraint)
{
    if (!constraint->surface) return false;
    auto* seat = server->seat.get();
    return seat->keyboard->focused_surface == constraint->surface
        && seat->pointer->focused_surface == constraint->surface;
}

static
vec2f64 apply_constraint(wroc_pointer_constraint* constraint, vec2f64 old_pos, vec2f64 pos, bool* in_constriant = nullptr)
{
    auto* surface = constraint->surface.get();

    vec2f64 original;
    switch (constraint->type) {
        break;case wroc_pointer_constraint_type::locked:   original = wroc_surface_pos_from_global(surface, old_pos);
        break;case wroc_pointer_constraint_type::confined: original = wroc_surface_pos_from_global(surface, pos);
    }
    vec2f64 constrained = original;

    // Constrain to initial surface rectangle
    auto surface_rect = rect2f64(surface->buffer_dst);
    static constexpr double epsilon = 0.0001;
    surface_rect.origin += epsilon;
    surface_rect.extent -= epsilon * 2;
    constrained = wrei_rect_clamp_point(surface_rect, constrained);

    // Constrain to surface input region
    if (!surface->current.input_region.empty()) {
        constrained = surface->current.input_region.constrain(constrained);
    }

    // Constrain to constraint region
    if (!constraint->current.region.empty()) {
        constrained = constraint->current.region.constrain(constrained);
    }

    if (in_constriant) *in_constriant = (constrained == original);

    return wroc_surface_pos_to_global(surface, constrained);
}

// -----------------------------------------------------------------------------

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

void wroc_pointer::absolute(wroc_output* output, vec2f64 offset)
{
    target->position = output->layout_rect.origin + offset;

    wroc_post_event(wroc_pointer_event {
        .type = wroc_event_type::pointer_motion,
        .pointer = target.get(),
        .motion = {},
    });
}

struct wroc_pointer_accel_config
{
    f64 offset;
    f64 rate;
    f64 multiplier;
};

static constexpr wroc_pointer_accel_config pointer_accel     = { 2.0, 0.05, 0.3 };
static constexpr wroc_pointer_accel_config pointer_rel_accel = { 2.0, 0.05, 1.0 };

static
vec2f64 pointer_acceleration_apply(const wroc_pointer_accel_config& config, vec2f64 delta, vec2f64* remainder = nullptr)
{
    f64 speed = glm::length(delta);
    vec2f64 sens = vec2f64(config.multiplier * (1 + (std::max(speed, config.offset) - config.offset) * config.rate));

    vec2f64 new_delta = sens * delta;

    if (!remainder) return new_delta;

    *remainder += new_delta;
    vec2f64 integer_delta = wrei_round_to_zero(*remainder);
    *remainder -= integer_delta;

    return integer_delta;
}

void wroc_pointer::relative(vec2f64 rel_unaccel)
{
    auto rel = pointer_acceleration_apply(pointer_rel_accel, rel_unaccel, &rel_remainder);
    auto delta = pointer_acceleration_apply(pointer_accel, rel_unaccel);

    auto old_pos = target->position;
    target->position = wroc_output_layout_clamp_position(server->output_layout.get(), target->position + delta);

    if (auto* constraint = server->seat->pointer->active_constraint.get(); constraint && is_constraint_focused(constraint)) {
        target->position = apply_constraint(constraint, old_pos, target->position);
    }

    wroc_post_event(wroc_pointer_event {
        .type = wroc_event_type::pointer_motion,
        .pointer = target.get(),
        .motion = {
            .rel = rel,
            .rel_unaccel = rel_unaccel,
        },
    });
}

void wroc_pointer::scroll(vec2f64 delta)
{
    // TODO: Separate axis and scroll events
    wroc_post_event(wroc_pointer_event {
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

        if (button == BTN_SIDE) {
            u32 translated = server->main_mod_evdev;
            switch (action) {
                break;case wroc_key_action::press:   pointer->keyboard->press(translated);
                break;case wroc_key_action::release: pointer->keyboard->release(translated);
                break;case wroc_key_action::enter:   pointer->keyboard->enter({translated});
            }
            continue;
        }

        if (action == wroc_key_action::release ? pointer->pressed.dec(button) : pointer->pressed.inc(button)) {
            // log_trace("button {} - {}", libevdev_event_code_get_name(EV_KEY, button), wrei_enum_to_string(action));
            if (action != wroc_key_action::enter) {
                wroc_post_event(wroc_pointer_event {
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
        wroc_send(wl_pointer_send_frame, pointer);
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
    if (focused_surface != pointer->focused_surface.get()) {
        if (auto* old_surface = pointer->focused_surface.get(); old_surface && old_surface->resource) {
            log_info("Leaving surface: {}", (void*)pointer->focused_surface.get());
            if (auto* constraint = wroc_surface_get_addon<wroc_pointer_constraint>(focused_surface)) {
                constraint->deactivate();
            }
            auto serial = wl_display_next_serial(server->display);
            for (auto* resource : pointer->resources) {
                if (!wroc_pointer_resource_matches_focus_client(pointer, resource)) continue;
                wroc_send(wl_pointer_send_leave, resource, serial, old_surface->resource);
                wroc_pointer_send_frame(resource);
            }
        }
        pointer->focused_surface = nullptr;

        if (focused_surface) {
            log_info("Entering surface: {}", (void*)focused_surface);

            pointer->focused_surface = focused_surface;

            auto pos = wroc_surface_pos_from_global(focused_surface, pointer->position);
            auto serial = wl_display_next_serial(server->display);

            for (auto* resource : pointer->resources) {
                if (!wroc_pointer_resource_matches_focus_client(pointer, resource)) continue;
                wroc_send(wl_pointer_send_enter, resource,
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

    wroc_toplevel* toplevel_under_cursor;
    auto surface_under_cursor = wroc_get_surface_under_cursor(&toplevel_under_cursor);

    if (seat->keyboard && pressed) {
        if (toplevel_under_cursor) {
            log_debug("trying to enter keyboard...");
            wroc_keyboard_enter(seat->keyboard.get(), toplevel_under_cursor->surface.get());
        } else {
            wroc_keyboard_clear_focus(seat->keyboard.get());
        }
    }

    if (surface_under_cursor) {
        if (pressed && pointer->pressed.size() == 1) {
            log_info("Starting implicit grab");
            server->implicit_grab_surface = surface_under_cursor;
        }
    }

    auto serial = wl_display_next_serial(server->display);
    auto time = wroc_get_elapsed_milliseconds();
    for (auto* resource : pointer->resources) {
        if (!wroc_pointer_resource_matches_focus_client(pointer, resource)) continue;
        wroc_send(wl_pointer_send_button, resource,
            serial,
            time,
            button, pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
        wroc_pointer_send_frame(resource);
    }

    if (!pressed && pointer->pressed.empty() && server->implicit_grab_surface) {
        log_info("Ending implicit grab");
        wroc_data_manager_finish_drag();
        server->implicit_grab_surface = {};
        wroc_pointer_update_focus(pointer, surface_under_cursor);
    }
}

static
void wroc_pointer_motion(wroc_seat_pointer* pointer, vec2f64 rel, vec2f64 _rel_unaccel)
{
    // log_trace("pointer({:.3f}, {:.3f})", pos.x, pos.y);

    wroc_toplevel* toplevel_under_cursor = nullptr;
    auto surface_under_cursor = wroc_get_surface_under_cursor(&toplevel_under_cursor);

    auto* focused_surface = server->implicit_grab_surface ? server->implicit_grab_surface.get() : surface_under_cursor;

    if (focused_surface && focused_surface->resource) {
        for (auto* resource : pointer->relative_pointers) {
            if (!wroc_pointer_resource_matches_focus_client(pointer, resource)) continue;

            if (wroc_is_client_behind(wroc_resource_get_client(resource))) {
                log_warn("[{}], client is running behind, skipping pointer relative event...", wroc_get_elapsed_milliseconds());
                continue;
            }

            auto time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch());
            auto time_us = time.count();

            // log_warn("relative[{}:{}] - {}, {}", time_us >> 32, time_us & 0xFFFF'FFFF, wrei_to_string(rel), wrei_to_string(rel_unaccel));

            bool force_accel = toplevel_under_cursor
                && toplevel_under_cursor->tweaks.force_accel
                && wroc_resource_get_client(toplevel_under_cursor->resource) == wl_resource_get_client(resource);

            auto rel_unaccel = force_accel ? rel : _rel_unaccel;

            wroc_send(zwp_relative_pointer_v1_send_relative_motion, resource,
                time_us >> 32, time_us & 0xFFFF'FFFF,
                wl_fixed_from_double(rel.x), wl_fixed_from_double(rel.y),
                wl_fixed_from_double(rel_unaccel.x), wl_fixed_from_double(rel_unaccel.y));
        }
    }

    wroc_data_manager_update_drag(surface_under_cursor);

    // log_trace("motion, grab = {}", (void*)server->implicit_grab_surface.surface.get());
    wroc_pointer_update_focus(pointer, focused_surface);

    if (auto* constraint = wroc_surface_get_addon<wroc_pointer_constraint>(focused_surface)) {
        constraint->activate();
    }

    if (focused_surface && focused_surface->resource) {

        auto time = wroc_get_elapsed_milliseconds();
        auto pos = wroc_surface_pos_from_global(focused_surface, pointer->position);

        // log_trace("sending motion to surface: {} ({:.2f}, {:.2f}) [{}]", (void*)focused_surface, pos.x, pos.y, time);

        for (auto* resource : pointer->resources) {
            if (!wroc_pointer_resource_matches_focus_client(pointer, resource)) continue;

            if (wroc_is_client_behind(wroc_resource_get_client(resource))) {
                log_warn("[{}], client is running behind, skipping motion event...", time);
                continue;
            }

            wroc_send(wl_pointer_send_motion, resource,
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

    auto time = wroc_get_elapsed_milliseconds();
    for (auto* resource : pointer->resources) {
        if (!wroc_pointer_resource_matches_focus_client(pointer, resource)) continue;

        auto version = wl_resource_get_version(resource);

        if (version >= WL_POINTER_AXIS_SOURCE_SINCE_VERSION) {
            wroc_send(wl_pointer_send_axis_source, resource, WL_POINTER_AXIS_SOURCE_WHEEL);
        }

        // TODO: We shouldn't have to send this for higher version clients
        constexpr double discrete_to_pixels = 15;
        if (rel.x) wroc_send(wl_pointer_send_axis, resource, time, WL_POINTER_AXIS_HORIZONTAL_SCROLL, wl_fixed_from_double(rel.x * discrete_to_pixels));
        if (rel.y) wroc_send(wl_pointer_send_axis, resource, time, WL_POINTER_AXIS_VERTICAL_SCROLL,   wl_fixed_from_double(rel.y * discrete_to_pixels));

        if (version >= WL_POINTER_AXIS_VALUE120_SINCE_VERSION) {
            if (rel.x) wroc_send(wl_pointer_send_axis_value120, resource, WL_POINTER_AXIS_HORIZONTAL_SCROLL, i32(rel.x * 120));
            if (rel.y) wroc_send(wl_pointer_send_axis_value120, resource, WL_POINTER_AXIS_VERTICAL_SCROLL,   i32(rel.y * 120));
        } else if (version >= WL_POINTER_AXIS_DISCRETE_SINCE_VERSION) {
            // TODO: Accumulate fractional values
            if (rel.x) wroc_send(wl_pointer_send_axis_discrete, resource, WL_POINTER_AXIS_HORIZONTAL_SCROLL, i32(rel.x));
            if (rel.y) wroc_send(wl_pointer_send_axis_discrete, resource, WL_POINTER_AXIS_VERTICAL_SCROLL,   i32(rel.y));
        }

        if (version >= WL_POINTER_FRAME_SINCE_VERSION) {
            wroc_pointer_send_frame(resource);
        }
    }
}

void wroc_handle_pointer_event(const wroc_pointer_event& event)
{
    switch (event.type) {
        break;case wroc_event_type::pointer_button:
            wroc_pointer_button(event.pointer, event.button.button, event.button.pressed);
        break;case wroc_event_type::pointer_motion:
            wroc_pointer_motion(event.pointer, event.motion.rel, event.motion.rel_unaccel);
        break;case wroc_event_type::pointer_axis:
            wroc_pointer_axis(event.pointer, event.axis.delta);
        break;default:
    }
}
