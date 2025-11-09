#include "backend.hpp"

#include <sys/mman.h>
#include <unistd.h>

// -----------------------------------------------------------------------------

static
void listen_wl_pointer_enter(void* data, wl_pointer*, u32 /* serial */, wl_surface* surface, wl_fixed_t sx, wl_fixed_t sy)
{
    log_info("pointer_axis_enter");

    auto* pointer = static_cast<WaylandPointer*>(data);
    pointer->current_output = backend_find_output_for_surface(pointer->display->backend, surface);
    pointer_absolute(pointer, pointer->current_output, {wl_fixed_to_double(sx), wl_fixed_to_double(sy)});
}

static
void listen_wl_pointer_leave(void* /* data */, wl_pointer*, u32 /* serial */, wl_surface*)
{
    log_info("pointer_axis_leave");
}

static
void listen_wl_pointer_motion(void* data, wl_pointer*, u32 /* time */, wl_fixed_t sx, wl_fixed_t sy)
{
    auto* pointer = static_cast<WaylandPointer*>(data);
    pointer_absolute(pointer, pointer->current_output, {wl_fixed_to_double(sx), wl_fixed_to_double(sy)});
}

static
void listen_wl_pointer_button(void* data, wl_pointer*, u32 /* serial */, u32 /* time */, u32 button, u32 state)
{
    auto* pointer = static_cast<WaylandPointer*>(data);
    log_debug("pointer_button({} = {})", libevdev_event_code_get_name(EV_KEY, button), state == WL_POINTER_BUTTON_STATE_PRESSED ? "press" : "release");
    pointer_button(pointer, button, state == WL_POINTER_BUTTON_STATE_PRESSED);
}

static
void listen_wl_pointer_axis(void* data, wl_pointer*, u32 /* time */, u32 axis, wl_fixed_t value)
{
    log_debug("pointer_axis(axis = {}, value = {})", magic_enum::enum_name(wl_pointer_axis(axis)), wl_fixed_to_double(value));

    auto* pointer = static_cast<WaylandPointer*>(data);
    pointer_axis(pointer, {
        axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL ? wl_fixed_to_double(value) : 0.0,
        axis == WL_POINTER_AXIS_VERTICAL_SCROLL   ? wl_fixed_to_double(value) : 0.0,
    });
}

static
void listen_wl_pointer_frame(void* /* data */, wl_pointer*)
{
    // log_info("pointer_axis_frame");
}

static
void listen_wl_pointer_axis_source(void* /* data */, wl_pointer*, u32 axis_source)
{
    log_debug("pointer_axis_source({})", magic_enum::enum_name(wl_pointer_axis_source(axis_source)));
}

static
void listen_wl_pointer_axis_stop(void* /* data */, wl_pointer*, u32 /* time */, u32 axis)
{
    log_debug("pointer_axis_stop({})", magic_enum::enum_name(wl_pointer_axis(axis)));
}

static
void listen_wl_pointer_axis_discrete(void* /* data */, wl_pointer*, u32 axis, i32 discrete)
{
    log_debug("pointer_axis_discrete(axis = {}, value = {})", magic_enum::enum_name(wl_pointer_axis(axis)), discrete);
}

static
void listen_wl_pointer_axis_value120(void* /* data */, wl_pointer*, u32 axis, i32 value120)
{
    log_debug("pointer_axis_value120(axis = {}, value = {})", magic_enum::enum_name(wl_pointer_axis(axis)), value120);
}

static
void listen_wl_pointer_axis_relative_direction(void* /* data */, wl_pointer*, u32 axis, u32 direction)
{
    log_debug("pointer_axis_relative_direction(axis = {}, direction = {})",
        magic_enum::enum_name(wl_pointer_axis(axis)),
        magic_enum::enum_name(wl_pointer_axis_relative_direction(direction)));
}

const wl_pointer_listener listeners::wl_pointer {
    .enter                   = listen_wl_pointer_enter,
    .leave                   = listen_wl_pointer_leave,
    .motion                  = listen_wl_pointer_motion,
    .button                  = listen_wl_pointer_button,
    .axis                    = listen_wl_pointer_axis,
    .frame                   = listen_wl_pointer_frame,
    .axis_source             = listen_wl_pointer_axis_source,
    .axis_stop               = listen_wl_pointer_axis_stop,
    .axis_discrete           = listen_wl_pointer_axis_discrete,
    .axis_value120           = listen_wl_pointer_axis_value120,
    .axis_relative_direction = listen_wl_pointer_axis_relative_direction,
};

static
void pointer_destroy(Backend* backend)
{
    if (!backend->keyboard) return;

    log_debug("pointer_destroy({})", (void*)backend->keyboard);

    wl_keyboard_release(backend->keyboard->wl_keyboard);
    xkb_keymap_unref(backend->keyboard->xkb_keymap);
    xkb_state_unref(backend->keyboard->xkb_state);
    xkb_context_unref(backend->keyboard->xkb_context);

    delete backend->keyboard;
}

static
void pointer_set(Backend* backend, struct wl_pointer* wl_pointer)
{
    if (!backend->pointer || backend->pointer->wl_pointer != wl_pointer) {
        log_debug("pointer_set({}, old = {})", (void*)wl_pointer, (void*)(backend->pointer ? backend->pointer->wl_pointer : nullptr));
    }

    if (backend->pointer && backend->pointer->wl_pointer != wl_pointer) {
        pointer_destroy(backend);
    }

    auto* pointer = backend->pointer = new WaylandPointer {};
    pointer->wl_pointer = wl_pointer;
    pointer->display = backend->display;

    wl_pointer_add_listener(wl_pointer, &listeners::wl_pointer, pointer);
}

// -----------------------------------------------------------------------------

static
void listen_wl_keyboard_keymap(void* data, wl_keyboard* keyboard, u32 format, i32 fd, u32 size)
{
    auto kb = static_cast<WaylandKeyboard*>(data);
    kb->wl_keyboard = keyboard;

    defer { close(fd); };

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        log_error("unsupported keyboard keymap type");
        return;
    }

    char* map_shm = static_cast<char*>(unix_check_null(mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0)));
    defer { munmap(map_shm, size); };

    auto* keymap = xkb_keymap_new_from_string(kb->xkb_context, map_shm, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    auto* state = xkb_state_new(keymap);

    xkb_keymap_unref(kb->xkb_keymap);
    xkb_state_unref(kb->xkb_state);

    kb->xkb_keymap = keymap;
    kb->xkb_state = state;
}

static
void listen_wl_keyboard_enter(void* data, wl_keyboard*, u32 /* serial */, wl_surface*, wl_array* key_array)
{
    auto kb = static_cast<WaylandKeyboard*>(data);

    log_debug("keyboard enter:");
    for (u32 keycode : to_span<u32>(key_array)) {
        kb->pressed[keycode] = true;
        keyboard_key(kb, keycode, true);
    }
}

static
void listen_wl_keyboard_key(void* data, wl_keyboard*, u32 /* serial */, u32 /* time */, u32 keycode, u32 state)
{
    auto kb = static_cast<WaylandKeyboard*>(data);

    if (state != WL_KEYBOARD_KEY_STATE_REPEATED) {
        bool pressed = state == WL_KEYBOARD_KEY_STATE_PRESSED;
        kb->pressed[keycode] = pressed;
        keyboard_key(kb, keycode, pressed);
    }
}

static
void listen_wl_keyboard_leave(void* data, wl_keyboard*, u32 /* serial */, wl_surface*)
{
    auto kb = static_cast<WaylandKeyboard*>(data);

    log_debug("keyboard leave");

    for (auto[keycode, pressed] : kb->pressed | std::views::enumerate) {
        if (pressed) keyboard_key(kb, keycode, false);
    }
    kb->pressed = {};
}

static
void listen_wl_keyboard_modifiers(void* data, wl_keyboard*, u32 /* serial */, u32 mods_depressed, u32 mods_latched, u32 mods_locked, u32 group)
{
    auto kb = static_cast<WaylandKeyboard*>(data);
    xkb_state_update_mask(kb->xkb_state, mods_depressed, mods_latched, mods_locked, 0, 0, group);
}

static
void listen_wl_keyboard_repeat_info(void* data, wl_keyboard*, i32 rate, i32 delay)
{
    auto kb = static_cast<WaylandKeyboard*>(data);
    log_debug("keyboard_repeat_info ( rate = {}, delay = {} )", rate, delay);
    kb->rate = rate;
    kb->delay = delay;
}

const wl_keyboard_listener listeners::wl_keyboard {
    .keymap      = listen_wl_keyboard_keymap,
    .enter       = listen_wl_keyboard_enter,
    .leave       = listen_wl_keyboard_leave,
    .key         = listen_wl_keyboard_key,
    .modifiers   = listen_wl_keyboard_modifiers,
    .repeat_info = listen_wl_keyboard_repeat_info,
};

static
void keyboard_destroy(Backend* backend)
{
    if (!backend->keyboard) return;

    log_debug("keyboard_destroy({})", (void*)backend->keyboard);

    wl_keyboard_release(backend->keyboard->wl_keyboard);
    xkb_keymap_unref(backend->keyboard->xkb_keymap);
    xkb_state_unref(backend->keyboard->xkb_state);
    xkb_context_unref(backend->keyboard->xkb_context);

    delete backend->keyboard;
}

static
void keyboard_set(Backend* backend, struct wl_keyboard* wl_keyboard)
{
    if (!backend->keyboard || backend->keyboard->wl_keyboard != wl_keyboard) {
        log_debug("keyboard_set({}, old = {})", (void*)wl_keyboard, (void*)(backend->keyboard ? backend->keyboard->wl_keyboard : nullptr));
    }

    if (backend->keyboard && backend->keyboard->wl_keyboard != wl_keyboard) {
        keyboard_destroy(backend);
    }

    auto* keyboard = backend->keyboard = new WaylandKeyboard {};
    keyboard->wl_keyboard = wl_keyboard;
    keyboard->display = backend->display;

    keyboard->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    wl_keyboard_add_listener(wl_keyboard, &listeners::wl_keyboard, keyboard);
}

// -----------------------------------------------------------------------------

static
void listen_wl_seat_capabilities(void* data, wl_seat* seat, u32 capabilities)
{
    auto* backend = static_cast<Backend*>(data);
    log_debug("wl_seat::capabilities");

    if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
        keyboard_set(backend, wl_seat_get_keyboard(seat));
    } else if (backend->keyboard) {
        keyboard_destroy(backend);
    }

    if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
        pointer_set(backend, wl_seat_get_pointer(seat));
    } else if (backend->pointer) {
        pointer_destroy(backend);
    }
}

static
void listen_wl_seat_name(void* /* data */, struct wl_seat*, const char* name)
{
    log_debug("wl_seat::name({})", name);
}

const wl_seat_listener listeners::wl_seat {
    .capabilities = listen_wl_seat_capabilities,
    .name = listen_wl_seat_name,
};
