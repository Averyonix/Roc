#include "backend.hpp"

#include "wroc/util.hpp"

#include "wroc/event.hpp"

// -----------------------------------------------------------------------------

#if !WROC_BACKEND_RELATIVE_POINTER
static
void wroc_backend_pointer_absolute(wroc_wayland_pointer* pointer, wl_fixed_t sx, wl_fixed_t sy)
{
    vec2f64 pos = {wl_fixed_to_double(sx), wl_fixed_to_double(sy)};
    // TODO: To logical coordinates
    pointer->absolute(pointer->current_output.get(), pos);
}
#endif

static
void wroc_listen_wl_pointer_enter(void* data, wl_pointer*, u32 serial, wl_surface* surface, wl_fixed_t sx, wl_fixed_t sy)
{
    log_info("pointer_enter");

    auto* pointer = static_cast<wroc_wayland_pointer*>(data);
    pointer->last_serial = serial;
#if !WROC_BACKEND_RELATIVE_POINTER
    pointer->current_output = wroc_wayland_backend_find_output_for_surface(static_cast<wroc_wayland_backend*>(pointer->server->backend.get()), surface);

    wroc_backend_pointer_absolute(pointer, sx, sy);
#endif

    wl_pointer_set_cursor(pointer->wl_pointer, serial, nullptr, 0, 0);
}

static
void wroc_listen_wl_pointer_leave(void* data, wl_pointer*, u32 serial, wl_surface*)
{
    log_info("pointer_leave");

    auto* pointer = static_cast<wroc_wayland_pointer*>(data);
    pointer->last_serial = serial;

    pointer->leave();
}

static
void wroc_listen_wl_pointer_motion(void* data, wl_pointer*, u32 time, wl_fixed_t sx, wl_fixed_t sy)
{
#if !WROC_BACKEND_RELATIVE_POINTER
    auto* pointer = static_cast<wroc_wayland_pointer*>(data);

    wroc_backend_pointer_absolute(pointer, sx, sy);
#endif
}

static
void wroc_listen_wl_pointer_button(void* data, wl_pointer*, u32 serial, u32 time, u32 button, u32 state)
{
    auto* pointer = static_cast<wroc_wayland_pointer*>(data);
    pointer->last_serial = serial;

    log_debug("pointer_button({} = {})", libevdev_event_code_get_name(EV_KEY, button), state == WL_POINTER_BUTTON_STATE_PRESSED ? "press" : "release");

    switch (state) {
        break;case WL_POINTER_BUTTON_STATE_PRESSED:  pointer->press(button);
        break;case WL_POINTER_BUTTON_STATE_RELEASED: pointer->release(button);
    }
}

static
void wroc_listen_wl_pointer_axis(void* data, wl_pointer*, u32 time, u32 axis, wl_fixed_t value)
{
    log_debug("pointer_axis(axis = {}, value = {})", magic_enum::enum_name(wl_pointer_axis(axis)), wl_fixed_to_double(value));
}

static
void wroc_listen_wl_pointer_frame(void* data, wl_pointer*)
{
    // log_info("pointer_frame");
}

static
void wroc_listen_wl_pointer_axis_source(void* data, wl_pointer*, u32 axis_source)
{
    log_debug("pointer_axis_source({})", magic_enum::enum_name(wl_pointer_axis_source(axis_source)));
}

static
void wroc_listen_wl_pointer_axis_stop(void* data, wl_pointer*, u32 time, u32 axis)
{
    log_debug("pointer_axis_stop({})", magic_enum::enum_name(wl_pointer_axis(axis)));
}

static
void wroc_listen_wl_pointer_axis_discrete(void* data, wl_pointer*, u32 axis, i32 discrete)
{
    log_debug("pointer_axis_discrete(axis = {}, value = {})", magic_enum::enum_name(wl_pointer_axis(axis)), discrete);
}

static
void wroc_listen_wl_pointer_axis_value120(void* data, wl_pointer*, u32 axis, i32 value120)
{
    log_debug("pointer_axis_value120(axis = {}, value = {})", magic_enum::enum_name(wl_pointer_axis(axis)), value120);

    auto* pointer = static_cast<wroc_wayland_pointer*>(data);

    pointer->scroll({
        axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL ? (value120 / 120.0) : 0.0,
        axis == WL_POINTER_AXIS_VERTICAL_SCROLL   ? (value120 / 120.0) : 0.0,
    });
}

static
void wroc_listen_wl_pointer_axis_relative_direction(void* data, wl_pointer*, u32 axis, u32 direction)
{
    log_debug("pointer_axis_relative_direction(axis = {}, direction = {})",
        magic_enum::enum_name(wl_pointer_axis(axis)),
        magic_enum::enum_name(wl_pointer_axis_relative_direction(direction)));
}

const wl_pointer_listener wroc_wl_pointer_listener {
    .enter                   = wroc_listen_wl_pointer_enter,
    .leave                   = wroc_listen_wl_pointer_leave,
    .motion                  = wroc_listen_wl_pointer_motion,
    .button                  = wroc_listen_wl_pointer_button,
    .axis                    = wroc_listen_wl_pointer_axis,
    .frame                   = wroc_listen_wl_pointer_frame,
    .axis_source             = wroc_listen_wl_pointer_axis_source,
    .axis_stop               = wroc_listen_wl_pointer_axis_stop,
    .axis_discrete           = wroc_listen_wl_pointer_axis_discrete,
    .axis_value120           = wroc_listen_wl_pointer_axis_value120,
    .axis_relative_direction = wroc_listen_wl_pointer_axis_relative_direction,
};

// -----------------------------------------------------------------------------

#if WROC_BACKEND_RELATIVE_POINTER
static
void relative_motion(void* data,
    zwp_relative_pointer_v1* zwp_relative_pointer_v1,
    u32 utime_hi,
    u32 utime_lo,
    wl_fixed_t dx,
    wl_fixed_t dy,
    wl_fixed_t dx_unaccel,
    wl_fixed_t dy_unaccel)
{
    auto* pointer = static_cast<wroc_wayland_pointer*>(data);
    auto delta = vec2f64(wl_fixed_to_double(dx_unaccel), wl_fixed_to_double(dy_unaccel));
    pointer->relative(delta);
}

const zwp_relative_pointer_v1_listener wroc_zwp_relative_pointer_v1_listener {
    .relative_motion = relative_motion,
};
#endif

wroc_wayland_pointer::~wroc_wayland_pointer()
{
    wl_pointer_release(wl_pointer);
#if WROC_BACKEND_RELATIVE_POINTER
    zwp_relative_pointer_v1_destroy(relative_pointer);
#endif
}

static
void wroc_pointer_set(wroc_wayland_backend* backend, struct wl_pointer* wl_pointer)
{
    if (!backend->pointer || backend->pointer->wl_pointer != wl_pointer) {
        log_debug("pointer_set({}, old = {})", (void*)wl_pointer, (void*)(backend->pointer ? backend->pointer->wl_pointer : nullptr));
    }

    if (backend->pointer && backend->pointer->wl_pointer == wl_pointer) {
        return;
    }

    auto* pointer = (backend->pointer = wrei_create<wroc_wayland_pointer>()).get();
    pointer->wl_pointer = wl_pointer;

    wl_pointer_add_listener(wl_pointer, &wroc_wl_pointer_listener, pointer);

#if WROC_BACKEND_RELATIVE_POINTER
    pointer->relative_pointer = zwp_relative_pointer_manager_v1_get_relative_pointer(backend->relative_pointer_manager, wl_pointer);
    zwp_relative_pointer_v1_add_listener(pointer->relative_pointer, &wroc_zwp_relative_pointer_v1_listener, pointer);
    for (auto& output : backend->outputs) {
        wroc_wayland_backend_update_pointer_constraint(output.get());
    }
#endif

    server->seat->pointer->attach(pointer);
}

// -----------------------------------------------------------------------------

static
void wroc_listen_wl_keyboard_keymap(void* data, wl_keyboard*, u32 format, i32 fd, u32 size)
{
    // Upstream keymaps are ignored
    close(fd);
}

static
void wroc_listen_wl_keyboard_enter(void* data, wl_keyboard*, u32 serial, wl_surface*, wl_array* key_array)
{
    auto kb = static_cast<wroc_wayland_keyboard*>(data);

    kb->enter(wroc_to_span<u32>(key_array));
}

static
void wroc_listen_wl_keyboard_key(void* data, wl_keyboard*, u32 serial, u32 time, u32 keycode, u32 state)
{
    auto kb = static_cast<wroc_wayland_keyboard*>(data);

    switch (state) {
        break;case WL_KEYBOARD_KEY_STATE_PRESSED:  kb->press(keycode);
        break;case WL_KEYBOARD_KEY_STATE_RELEASED: kb->release(keycode);
    }
}

static
void wroc_listen_wl_keyboard_leave(void* data, wl_keyboard*, u32 serial, wl_surface*)
{
    auto kb = static_cast<wroc_wayland_keyboard*>(data);

    kb->leave();
}

static
void wroc_listen_wl_keyboard_modifiers(void* data, wl_keyboard*, u32 serial, u32 mods_depressed, u32 mods_latched, u32 mods_locked, u32 group)
{
    // Upstream modifier state is ignored
}

static
void wroc_listen_wl_keyboard_repeat_info(void* data, wl_keyboard*, i32 rate, i32 delay)
{
    // Upstream repeat info is ignored
}

const wl_keyboard_listener wroc_wl_keyboard_listener {
    .keymap      = wroc_listen_wl_keyboard_keymap,
    .enter       = wroc_listen_wl_keyboard_enter,
    .leave       = wroc_listen_wl_keyboard_leave,
    .key         = wroc_listen_wl_keyboard_key,
    .modifiers   = wroc_listen_wl_keyboard_modifiers,
    .repeat_info = wroc_listen_wl_keyboard_repeat_info,
};

wroc_wayland_keyboard::~wroc_wayland_keyboard()
{
    wl_keyboard_release(wl_keyboard);
}

static
void wroc_keyboard_set(wroc_wayland_backend* backend, struct wl_keyboard* wl_keyboard)
{
    if (!backend->keyboard || backend->keyboard->wl_keyboard != wl_keyboard) {
        log_debug("keyboard_set({}, old = {})", (void*)wl_keyboard, (void*)(backend->keyboard ? backend->keyboard->wl_keyboard : nullptr));
    }

    if (backend->keyboard && backend->keyboard->wl_keyboard == wl_keyboard) {
        return;
    }

    auto* keyboard = (backend->keyboard = wrei_create<wroc_wayland_keyboard>()).get();
    keyboard->wl_keyboard = wl_keyboard;

    wl_keyboard_add_listener(wl_keyboard, &wroc_wl_keyboard_listener, keyboard);

    server->seat->keyboard->attach(keyboard);
}

// -----------------------------------------------------------------------------

static
void wroc_listen_wl_seat_capabilities(void* data, wl_seat* seat, u32 capabilities)
{
    auto* backend = static_cast<wroc_wayland_backend*>(data);
    log_debug("wl_seat::capabilities");

    if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
        wroc_keyboard_set(backend, wl_seat_get_keyboard(seat));
    } else if (backend->keyboard) {
        backend->keyboard = nullptr;
    }

    if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
        wroc_pointer_set(backend, wl_seat_get_pointer(seat));
    } else if (backend->pointer) {
        backend->pointer = nullptr;
    }
}

static
void wroc_listen_wl_seat_name(void* data, struct wl_seat*, const char* name)
{
    log_debug("wl_seat::name({})", name);
}

const wl_seat_listener wroc_wl_seat_listener {
    .capabilities = wroc_listen_wl_seat_capabilities,
    .name         = wroc_listen_wl_seat_name,
};
