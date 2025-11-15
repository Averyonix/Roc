#include "server.hpp"
#include "util.hpp"

#include "wrei/shm.hpp"

#include "wroc/event.hpp"

wroc_keyboard::~wroc_keyboard()
{
    xkb_keymap_unref(xkb_keymap);
    xkb_state_unref(xkb_state);
    xkb_context_unref(xkb_context);
}

static
void wroc_keyboard_update_active_modifiers(wroc_keyboard* kb)
{
    wroc_modifiers down = {};

    auto xkb_mods = xkb_state_serialize_mods(kb->xkb_state, XKB_STATE_MODS_EFFECTIVE);
    for (auto[mod, mask] : kb->xkb_mod_masks) {
        if (xkb_mods & mask) down |= mod;
    }

    kb->active_modifiers = down;
}

wroc_modifiers wroc_keyboard_get_active_modifiers(wroc_keyboard* kb)
{
    auto down = kb->active_modifiers;

    if (down >= kb->server->main_mod) {
        down |= wroc_modifiers::mod;
    }

    return down;
}

wroc_modifiers wroc_get_active_modifiers(wroc_server* server)
{
    wroc_modifiers mods = {};
    if (server->seat->keyboard) {
        mods |= wroc_keyboard_get_active_modifiers(server->seat->keyboard);
    }
    return mods;
}

static
void wroc_keyboard_added(wroc_keyboard* kb)
{
    kb->server->seat->keyboard = kb;
};

static
void wroc_keyboard_keymap_update(wroc_keyboard* kb)
{
    // Update modifier indices

    for (auto[i, mod_name] : wroc_modifier_xkb_names | std::views::enumerate) {
        auto[mod, xkb_name] = mod_name;
        kb->xkb_mod_masks[i] = { mod, xkb_keymap_mod_get_mask(kb->xkb_keymap, xkb_name) };
    }

    wroc_keyboard_update_active_modifiers(kb);

    // Update keymap file

    char* keymap_str = xkb_keymap_get_as_string(kb->xkb_keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
    if (!keymap_str) {
        log_error("Failed to get string version of keymap");
        return;
    }
    usz keymap_size = strlen(keymap_str) + 1;

    int rw_fd = -1;
    int ro_fd = -1;
    if (!wrei_allocate_shm_file_pair(keymap_size, &rw_fd, &ro_fd)) {
        log_error("Failed to allocate shm file for keymap");
        return;
    }

    void* dst = mmap(nullptr, keymap_size, PROT_READ | PROT_WRITE, MAP_SHARED, rw_fd, 0);
    close(rw_fd);
    if (dst == MAP_FAILED) {
        log_error("mmap failed");
        close(ro_fd);
        return;
    }

    memcpy(dst, keymap_str, keymap_size);
    munmap(dst, keymap_size);
    free(keymap_str);

    kb->keymap_fd = ro_fd;
    kb->keymap_size = keymap_size;

    log_debug("Successfully updated keyboard keymap fd: {}", kb->keymap_fd);

    for (wl_resource* resource : kb->wl_keyboards) {
        wl_keyboard_send_keymap(resource, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, kb->keymap_fd, kb->keymap_size);
    }
}

static
void wroc_keyboard_key(wroc_keyboard* kb, u32 libinput_keycode, bool pressed)
{
    u32 xkb_keycode = libinput_keycode + 8;
    char name[128] = {};
    char _utf[128] = {};

    if (!kb->focused && kb->wl_keyboards.front()) {
        wroc_surface* surface = nullptr;
        for (auto* s : kb->server->surfaces) {
            if (wroc_xdg_surface::try_from(s)) {
                surface = s;
            }
        }

        if (surface) {
            log_error("Sending keyboard enter!");
            kb->focused = kb->wl_keyboards.front();
            wl_keyboard_send_enter(kb->focused, wl_display_next_serial(kb->server->display),
                surface->wl_surface,
                wrei_ptr_to(wroc_to_wl_array<u32>({})));
            wl_keyboard_send_modifiers(kb->focused, wl_display_get_serial(kb->server->display), 0, 0, 0, 0);
        }
    }

    auto sym = xkb_state_key_get_one_sym(kb->xkb_state, xkb_keycode);
    xkb_keysym_get_name(sym, name, sizeof(name) - 1);

    xkb_state_key_get_utf8(kb->xkb_state, xkb_keycode, _utf, sizeof(_utf) - 1);
    auto utf = wrei_escape_utf8(_utf);

    if (strcmp(name, _utf) == 0) {
        log_debug("key '{}' ({}) = {}", utf, sym, pressed ? "press" : "release");
    } else if (!utf.empty()) {
        log_debug("key {} '{}' ({}) = {}", name, utf, sym, pressed ? "press" : "release");
    } else {
        log_debug("key {} ({}) = {}", name, sym, pressed ? "press" : "release");
    }

    if (kb->focused) {
        wl_keyboard_send_key(kb->focused,
            wl_display_next_serial(kb->server->display),
            wroc_get_elapsed_milliseconds(kb->server),
            libinput_keycode, pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
    }
}

static
void wroc_keyboard_modifiers(wroc_keyboard* kb, u32 mods_depressed, u32 mods_latched, u32 mods_locked, u32 group)
{
    wroc_keyboard_update_active_modifiers(kb);

    if (kb->focused) {
        wl_keyboard_send_modifiers(kb->focused,
            wl_display_next_serial(kb->server->display),
            mods_depressed,
            mods_latched,
            mods_locked,
            group);
    }
}

void wroc_handle_keyboard_event(wroc_server* server, const wroc_keyboard_event& event)
{
    switch (event.type) {
        case wroc_event_type::keyboard_added:
            wroc_keyboard_added(event.keyboard);
            break;
        case wroc_event_type::keyboard_keymap:
            wroc_keyboard_keymap_update(event.keyboard);
            break;
        case wroc_event_type::keyboard_key:
            wroc_keyboard_key(event.keyboard, event.key.keycode, event.key.pressed);
            break;
        case wroc_event_type::keyboard_modifiers:
            wroc_keyboard_modifiers(event.keyboard, event.mods.depressed, event.mods.latched, event.mods.locked, event.mods.group);
            break;
        default:
            break;
    }
}
