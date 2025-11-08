#include "backend.hpp"

#include <sys/mman.h>
#include <unistd.h>

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

static constexpr struct wl_keyboard_listener wl_keyboard_listener {
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

    keyboard->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    wl_keyboard_add_listener(wl_keyboard, &wl_keyboard_listener, keyboard);
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
