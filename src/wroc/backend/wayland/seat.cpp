#include "backend.hpp"

#include "wroc/util.hpp"

#include "wroc/event.hpp"

// -----------------------------------------------------------------------------

static
void wroc_backend_pointer_absolute(wroc_wayland_pointer* pointer, wl_fixed_t sx, wl_fixed_t sy)
{
    vec2f64 pos = {wl_fixed_to_double(sx), wl_fixed_to_double(sy)};
    pointer->layout_position = pos + vec2f64(pointer->current_output->position);
    wroc_post_event(pointer->server, wroc_pointer_event {
        .type = wroc_event_type::pointer_motion,
        .pointer = pointer,
        .output = pointer->current_output,
    });
}

static
void wroc_listen_wl_pointer_enter(void* data, wl_pointer*, u32 serial, wl_surface* surface, wl_fixed_t sx, wl_fixed_t sy)
{
    log_info("pointer_enter");

    auto* pointer = static_cast<wroc_wayland_pointer*>(data);
    pointer->last_serial = serial;
    pointer->current_output = wroc_wayland_backend_find_output_for_surface(static_cast<wroc_wayland_backend*>(pointer->server->backend.get()), surface);

    wroc_backend_pointer_absolute(pointer, sx, sy);

    wl_pointer_set_cursor(pointer->wl_pointer, serial, nullptr, 0, 0);
}

static
void wroc_listen_wl_pointer_leave(void* data, wl_pointer*, u32 serial, wl_surface*)
{
    log_info("pointer_leave");

    auto* pointer = static_cast<wroc_wayland_pointer*>(data);
    pointer->last_serial = serial;

    // NOTE: In theory no mouse buttons should every be left pressed at this point anyway?
    for (auto button : pointer->pressed) {
        wroc_post_event(pointer->server, wroc_pointer_event {
            .type = wroc_event_type::pointer_button,
            .pointer = pointer,
            .button { .button = button, .pressed = false },
        });
    }
    pointer->pressed.clear();
}

static
void wroc_listen_wl_pointer_motion(void* data, wl_pointer*, u32 /* time */, wl_fixed_t sx, wl_fixed_t sy)
{
    auto* pointer = static_cast<wroc_wayland_pointer*>(data);

    wroc_backend_pointer_absolute(pointer, sx, sy);
}

static
void update_pointer_button_state(wroc_wayland_pointer* pointer, u32 button, bool state)
{
    if (!state) {
        std::erase(pointer->pressed, button);
    } else if (std::ranges::find(pointer->pressed, button) == pointer->pressed.end()) {
        pointer->pressed.emplace_back(button);
    }
}

static
void wroc_listen_wl_pointer_button(void* data, wl_pointer*, u32 serial, u32 /* time */, u32 button, u32 state)
{
    auto* pointer = static_cast<wroc_wayland_pointer*>(data);
    pointer->last_serial = serial;
    log_debug("pointer_button({} = {})", libevdev_event_code_get_name(EV_KEY, button), state == WL_POINTER_BUTTON_STATE_PRESSED ? "press" : "release");
    update_pointer_button_state(pointer, button, state == WL_POINTER_BUTTON_STATE_PRESSED);
    wroc_post_event(pointer->server, wroc_pointer_event {
        .type = wroc_event_type::pointer_button,
        .pointer = pointer,
        .output = pointer->current_output,
        .button { .button = button, .pressed = state == WL_POINTER_BUTTON_STATE_PRESSED },
    });
}

static
void wroc_listen_wl_pointer_axis(void* data, wl_pointer*, u32 /* time */, u32 axis, wl_fixed_t value)
{
    log_debug("pointer_axis(axis = {}, value = {})", magic_enum::enum_name(wl_pointer_axis(axis)), wl_fixed_to_double(value));
}

static
void wroc_listen_wl_pointer_frame(void* /* data */, wl_pointer*)
{
    // log_info("pointer_frame");
}

static
void wroc_listen_wl_pointer_axis_source(void* /* data */, wl_pointer*, u32 axis_source)
{
    log_debug("pointer_axis_source({})", magic_enum::enum_name(wl_pointer_axis_source(axis_source)));
}

static
void wroc_listen_wl_pointer_axis_stop(void* /* data */, wl_pointer*, u32 /* time */, u32 axis)
{
    log_debug("pointer_axis_stop({})", magic_enum::enum_name(wl_pointer_axis(axis)));
}

static
void wroc_listen_wl_pointer_axis_discrete(void* /* data */, wl_pointer*, u32 axis, i32 discrete)
{
    log_debug("pointer_axis_discrete(axis = {}, value = {})", magic_enum::enum_name(wl_pointer_axis(axis)), discrete);
}

static
void wroc_listen_wl_pointer_axis_value120(void* data, wl_pointer*, u32 axis, i32 value120)
{
    log_debug("pointer_axis_value120(axis = {}, value = {})", magic_enum::enum_name(wl_pointer_axis(axis)), value120);

    auto* pointer = static_cast<wroc_wayland_pointer*>(data);
    wroc_post_event(pointer->server, wroc_pointer_event {
        .type = wroc_event_type::pointer_axis,
        .pointer = pointer,
        .output = pointer->current_output,
        .axis {
            .delta = {
                axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL ? (value120 / 120.0) : 0.0,
                axis == WL_POINTER_AXIS_VERTICAL_SCROLL   ? (value120 / 120.0) : 0.0,
            }
        },
    });
}

static
void wroc_listen_wl_pointer_axis_relative_direction(void* /* data */, wl_pointer*, u32 axis, u32 direction)
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

wroc_wayland_pointer::~wroc_wayland_pointer()
{
    wl_pointer_release(wl_pointer);
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

    auto* pointer = (backend->pointer = wrei_adopt_ref(wrei_get_registry(backend)->create<wroc_wayland_pointer>())).get();
    pointer->wl_pointer = wl_pointer;
    pointer->server = backend->server;

    wl_pointer_add_listener(wl_pointer, &wroc_wl_pointer_listener, pointer);
    wroc_post_event(pointer->server, wroc_pointer_event {
        .type = wroc_event_type::pointer_added,
        .pointer = pointer,
    });
}

// -----------------------------------------------------------------------------

static
void wroc_listen_wl_keyboard_keymap(void* data, wl_keyboard* keyboard, u32 format, i32 fd, u32 size)
{
    auto kb = static_cast<wroc_wayland_keyboard*>(data);
    kb->wl_keyboard = keyboard;

    defer { close(fd); };

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        log_error("unsupported keyboard keymap type");
        return;
    }

    char* map_shm = static_cast<char*>(wrei_unix_check_null(mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0)));
    defer { munmap(map_shm, size); };

    auto* keymap = xkb_keymap_new_from_string(kb->xkb_context, map_shm, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    auto* state = xkb_state_new(keymap);

    xkb_keymap_unref(kb->xkb_keymap);
    xkb_state_unref(kb->xkb_state);

    kb->xkb_keymap = keymap;
    kb->xkb_state = state;

    wroc_post_event(kb->server, wroc_keyboard_event {
        .type = wroc_event_type::keyboard_keymap,
        .keyboard = kb,
    });
}

static
void update_kb_key_state(wroc_keyboard* kb, u32 keycode, bool state)
{
    if (!state) {
        std::erase(kb->pressed, keycode);
    } else if (std::ranges::find(kb->pressed, keycode) == kb->pressed.end()) {
        kb->pressed.emplace_back(keycode);
    }
}

static
void wroc_listen_wl_keyboard_enter(void* data, wl_keyboard*, u32 /* serial */, wl_surface*, wl_array* key_array)
{
    auto kb = static_cast<wroc_wayland_keyboard*>(data);

    log_debug("keyboard enter:");
    for (u32 keycode : wroc_to_span<u32>(key_array)) {
        update_kb_key_state(kb, keycode, true);

        // TODO: Should we send an "enter" event?
    }
}

static
void wroc_listen_wl_keyboard_key(void* data, wl_keyboard*, u32 /* serial */, u32 /* time */, u32 keycode, u32 state)
{
    auto kb = static_cast<wroc_wayland_keyboard*>(data);

    if (state != WL_KEYBOARD_KEY_STATE_REPEATED) {
        bool pressed = state == WL_KEYBOARD_KEY_STATE_PRESSED;
        update_kb_key_state(kb, keycode, pressed);
        wroc_post_event(kb->server, wroc_keyboard_event {
            .type = wroc_event_type::keyboard_key,
            .keyboard = kb,
            .key { .keycode = keycode, .pressed = pressed },
        });
    }
}

static
void wroc_listen_wl_keyboard_leave(void* data, wl_keyboard*, u32 /* serial */, wl_surface*)
{
    auto kb = static_cast<wroc_wayland_keyboard*>(data);

    log_debug("keyboard leave");

    for (auto keycode : kb->pressed) {
        wroc_post_event(kb->server, wroc_keyboard_event {
            .type = wroc_event_type::keyboard_key,
            .keyboard = kb,
            .key { .keycode = u32(keycode), .pressed = false },
        });
    }
    kb->pressed.clear();
}

static
void wroc_listen_wl_keyboard_modifiers(void* data, wl_keyboard*, u32 /* serial */, u32 mods_depressed, u32 mods_latched, u32 mods_locked, u32 group)
{
    auto kb = static_cast<wroc_wayland_keyboard*>(data);
    xkb_state_update_mask(kb->xkb_state, mods_depressed, mods_latched, mods_locked, 0, 0, group);
    wroc_post_event(kb->server, wroc_keyboard_event {
        .type = wroc_event_type::keyboard_modifiers,
        .keyboard = kb,
        .mods {
            .depressed = mods_depressed,
            .latched   = mods_latched,
            .locked    = mods_locked,
            .group     = group,
        }
    });
}

static
void wroc_listen_wl_keyboard_repeat_info(void* data, wl_keyboard*, i32 rate, i32 delay)
{
    auto kb = static_cast<wroc_wayland_keyboard*>(data);
    log_debug("keyboard_repeat_info ( rate = {}, delay = {} )", rate, delay);
    kb->rate = rate;
    kb->delay = delay;
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

    auto* keyboard = (backend->keyboard = wrei_adopt_ref(wrei_get_registry(backend)->create<wroc_wayland_keyboard>())).get();
    keyboard->wl_keyboard = wl_keyboard;
    keyboard->server = backend->server;

    keyboard->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    wl_keyboard_add_listener(wl_keyboard, &wroc_wl_keyboard_listener, keyboard);
    wroc_post_event(keyboard->server, wroc_keyboard_event {
        .type = wroc_event_type::keyboard_added,
        .keyboard = keyboard,
    });
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
void wroc_listen_wl_seat_name(void* /* data */, struct wl_seat*, const char* name)
{
    log_debug("wl_seat::name({})", name);
}

const wl_seat_listener wroc_wl_seat_listener {
    .capabilities = wroc_listen_wl_seat_capabilities,
    .name         = wroc_listen_wl_seat_name,
};
