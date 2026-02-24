#include "wayland.hpp"

wrio_input_device_wayland_keyboard::~wrio_input_device_wayland_keyboard()
{
    wl_keyboard_destroy(wl_keyboard);
}

static
void keyboard_enter(void* udata, wl_keyboard*, u32 serial, wl_surface*, wl_array* keys)
{
    auto* ctx = static_cast<wrio_context*>(udata);
    auto* kb = ctx->wayland->keyboard.get();
    wrio_input_device_key_enter(kb, wrio_to_span<u32>(keys));
}

static
void keyboard_leave(void* udata, wl_keyboard*, u32 serial, wl_surface*)
{
    auto* ctx = static_cast<wrio_context*>(udata);
    auto* kb = ctx->wayland->keyboard.get();
    wrio_input_device_leave(kb);
}

static
void keyboard_key(void* udata, wl_keyboard*, u32 serial, u32 time, u32 keycode, u32 state)
{
    auto* ctx = static_cast<wrio_context*>(udata);
    auto* kb = ctx->wayland->keyboard.get();
    switch (state) {
        break;case WL_KEYBOARD_KEY_STATE_PRESSED:  wrio_input_device_key_press(  kb, keycode);
        break;case WL_KEYBOARD_KEY_STATE_RELEASED: wrio_input_device_key_release(kb, keycode);
    }
}

WRIO_WL_LISTENER(wl_keyboard) = {
    .keymap = [](void*, wl_keyboard*, u32, int fd, u32 size) { close(fd); },
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key   = keyboard_key,
    WRIO_WL_STUB_QUIET(modifiers),
    WRIO_WL_STUB_QUIET(repeat_info),
};

static
void set_keyboard(wrio_context* ctx)
{
    auto* kb = (ctx->wayland->keyboard = wrei_create<wrio_input_device_wayland_keyboard>()).get();
    kb->wl_keyboard = wl_seat_get_keyboard(ctx->wayland->wl_seat);
    kb->ctx = ctx;
    wl_keyboard_add_listener(kb->wl_keyboard, &wrio_wl_keyboard_listener, ctx);
    wrio_input_device_add(kb);
}

// -----------------------------------------------------------------------------

wrio_input_device_wayland_pointer::~wrio_input_device_wayland_pointer()
{
    zwp_relative_pointer_v1_destroy(zwp_relative_pointer_v1);
    wl_pointer_destroy(wl_pointer);
}

static
wrio_output_wayland* find_output_for_surface(wrio_context* ctx, wl_surface* surface)
{
    for (auto& output : ctx->wayland->outputs) {
        if (output->wl_surface == surface) {
            return output.get();
        }
    }

    return nullptr;
}

static
void pointer_enter(void* udata, wl_pointer*, u32 serial, wl_surface* surface, wl_fixed_t sx, wl_fixed_t sy)
{
    auto* ctx = static_cast<wrio_context*>(udata);
    auto* ptr = ctx->wayland->pointer.get();

    ptr->last_serial = serial;
    ptr->current_output = find_output_for_surface(ctx, surface);

    // Always hide parent compositor cursor
    wl_pointer_set_cursor(ptr->wl_pointer, serial, nullptr, 0, 0);
}

static
void pointer_leave(void* udata, wl_pointer*, u32 serial, wl_surface*)
{
    auto* ptr = static_cast<wrio_context*>(udata)->wayland->pointer.get();
    ptr->last_serial = serial;
    ptr->current_output = nullptr;
    wrio_input_device_leave(ptr);
}

static
void pointer_button(void* udata, wl_pointer*, u32 serial, u32 time, u32 button, u32 state)
{
    auto* ptr = static_cast<wrio_context*>(udata)->wayland->pointer.get();
    switch (state) {
        break;case WL_POINTER_BUTTON_STATE_PRESSED:  wrio_input_device_key_press(  ptr, button);
        break;case WL_POINTER_BUTTON_STATE_RELEASED: wrio_input_device_key_release(ptr, button);
    }
}

static
void pointer_axis_value120(void* udata, wl_pointer*, u32 axis, i32 value120)
{
    auto* ptr = static_cast<wrio_context*>(udata)->wayland->pointer.get();
    f32 value = value120 / 120.f;
    switch (axis) {
        break;case WL_POINTER_AXIS_HORIZONTAL_SCROLL: wrio_input_device_pointer_scroll(ptr, {value, 0});
        break;case WL_POINTER_AXIS_VERTICAL_SCROLL:   wrio_input_device_pointer_scroll(ptr, {0, value});
    }
}

WRIO_WL_LISTENER(wl_pointer) = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    WRIO_WL_STUB_QUIET(motion),
    .button = pointer_button,
    WRIO_WL_STUB_QUIET(axis),
    WRIO_WL_STUB_QUIET(frame),
    WRIO_WL_STUB_QUIET(axis_source),
    WRIO_WL_STUB_QUIET(axis_stop),
    WRIO_WL_STUB_QUIET(axis_discrete),
    .axis_value120 = pointer_axis_value120,
    WRIO_WL_STUB_QUIET(axis_relative_direction),
};

static
void pointer_relative(
    void* udata,
    zwp_relative_pointer_v1*,
    u32 utime_hi, u32 utime_lo,
    wl_fixed_t dx, wl_fixed_t dy,
    wl_fixed_t dx_unaccel, wl_fixed_t dy_unaccel)
{
    auto* ptr = static_cast<wrio_input_device_wayland_pointer*>(udata);
    auto* output = get_impl(ptr->current_output.get());
    if (!output || !output->pointer_locked) return;
    wrio_input_device_pointer_motion(ptr, {wl_fixed_to_double(dx_unaccel), wl_fixed_to_double(dy_unaccel)});
}

WRIO_WL_LISTENER(zwp_relative_pointer_v1) = {
    .relative_motion = pointer_relative,
};

static
void set_pointer(wrio_context* ctx)
{
    auto* ptr = (ctx->wayland->pointer = wrei_create<wrio_input_device_wayland_pointer>()).get();
    ptr->ctx = ctx;

    ptr->wl_pointer = wl_seat_get_pointer(ctx->wayland->wl_seat);
    wl_pointer_add_listener(ptr->wl_pointer, &wrio_wl_pointer_listener, ctx);

    ptr->zwp_relative_pointer_v1 = zwp_relative_pointer_manager_v1_get_relative_pointer(
        ctx->wayland->zwp_relative_pointer_manager_v1,
        ptr->wl_pointer);
    zwp_relative_pointer_v1_add_listener(ptr->zwp_relative_pointer_v1, &wrio_zwp_relative_pointer_v1_listener, ptr);

    wrio_input_device_add(ptr);
}

// -----------------------------------------------------------------------------

static
void seat_capabilities(void* udata, wl_seat*, u32 capabilities)
{
    auto* ctx = static_cast<wrio_context*>(udata);

    if      ( (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !ctx->wayland->keyboard) set_keyboard(ctx);
    else if (!(capabilities & WL_SEAT_CAPABILITY_KEYBOARD))    ctx->wayland->keyboard = nullptr;

    if      ( (capabilities & WL_SEAT_CAPABILITY_POINTER) && !ctx->wayland->pointer) set_pointer(ctx);
    else if (!(capabilities & WL_SEAT_CAPABILITY_POINTER))    ctx->wayland->pointer = nullptr;
}

WRIO_WL_LISTENER(wl_seat) = {
    .capabilities = seat_capabilities,
    WRIO_WL_STUB(wl_seat, name),
};
