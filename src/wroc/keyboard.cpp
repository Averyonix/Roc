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
bool wroc_keyboard_resource_matches_focus_client(wroc_keyboard* kb, wl_resource* resource)
{
    if (!kb->focused_surface) return false;
    if (!kb->focused_surface->resource) return false;
    return wroc_resource_get_client(resource) == wroc_resource_get_client(kb->focused_surface->resource);
}

void wroc_keyboard_clear_focus(wroc_keyboard* kb)
{
    if (auto* surface = kb->focused_surface.get(); surface && surface->resource) {
        auto serial = wl_display_next_serial(kb->server->display);

        for (auto* resource : kb->resources) {
            if (!wroc_keyboard_resource_matches_focus_client(kb, resource)) continue;
            for (auto keycode : kb->pressed) {
                wl_keyboard_send_key(resource,
                    serial,
                    wroc_get_elapsed_milliseconds(kb->server),
                    u32(keycode), WL_KEYBOARD_KEY_STATE_RELEASED);
            }
            wl_keyboard_send_modifiers(resource, serial, 0, 0, 0, 0);
            wl_keyboard_send_leave(resource, serial, surface->resource);
        }

        kb->focused_surface = nullptr;
    }
}

void wroc_keyboard_enter(wroc_keyboard* kb, wroc_surface* surface)
{
    if (surface == kb->focused_surface.get()) return;

    wroc_keyboard_clear_focus(kb);

    if (!surface->resource) return;

    // TODO: Consolidate "client seat" into a with client's wl_keyboard/wl_pointer handles
    //       To deduplicate code between this and `wroc_pointer`

    log_warn("KEYBOARD ENTERED");
    kb->focused_surface = surface;

    auto serial = wl_display_next_serial(kb->server->display);

    for (auto* resource : kb->resources) {
        if (!wroc_keyboard_resource_matches_focus_client(kb, resource)) continue;

        wroc_data_manager_offer_selection(kb->server, wroc_resource_get_client(resource));

        wl_keyboard_send_enter(resource,
            serial,
            surface->resource, wrei_ptr_to(wroc_to_wl_array<u32>(kb->pressed)));

        wl_keyboard_send_modifiers(resource,
            serial,
            xkb_state_serialize_mods(kb->xkb_state, XKB_STATE_MODS_DEPRESSED),
            xkb_state_serialize_mods(kb->xkb_state, XKB_STATE_MODS_LATCHED),
            xkb_state_serialize_mods(kb->xkb_state, XKB_STATE_MODS_LOCKED),
            xkb_state_serialize_layout(kb->xkb_state, XKB_STATE_LAYOUT_EFFECTIVE));

        wroc_surface_raise(surface);

        break;
    }
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

    for (auto[i, mod] : wroc_modifier_info | std::views::enumerate) {
        kb->xkb_mod_masks[i] = { mod.flag, xkb_keymap_mod_get_mask(kb->xkb_keymap, mod.name) };
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

    for (wl_resource* resource : kb->resources) {
        wl_keyboard_send_keymap(resource, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, kb->keymap_fd, kb->keymap_size);
    }
}

static
void wroc_debug_print_key(wroc_keyboard* kb, u32 libinput_keycode, bool pressed)
{
    u32 xkb_keycode = wroc_key_to_xkb(libinput_keycode);
    char name[128] = {};
    char _utf[128] = {};

    auto sym = xkb_state_key_get_one_sym(kb->xkb_state, xkb_keycode);
    xkb_keysym_get_name(sym, name, sizeof(name) - 1);

    xkb_state_key_get_utf8(kb->xkb_state, xkb_keycode, _utf, sizeof(_utf) - 1);
    auto utf = wrei_escape_utf8(_utf);

    if (strcmp(name, _utf) == 0) {
        log_debug("key '{}' ({}) (({})) = {}", utf, sym, libinput_keycode, pressed ? "press" : "release");
    } else if (!utf.empty()) {
        log_debug("key <{}> '{}' ({}) (({})) = {}", name, utf, sym, libinput_keycode, pressed ? "press" : "release");
    } else {
        log_debug("key <{}> ({}) (({})) = {}", name, sym, libinput_keycode, pressed ? "press" : "release");
    }
}

static
void wroc_keyboard_key(wroc_keyboard* kb, u32 keycode, bool pressed)
{
    wroc_debug_print_key(kb, keycode, pressed);

    {
        // TODO: Proper compositor keybind handling
        u32 xkb_keycode = wroc_key_to_xkb(keycode);
        auto sym = xkb_state_key_get_one_sym(kb->xkb_state, xkb_keycode);
        if (sym == XKB_KEY_Print && pressed) {
            log_debug("PRINT OVERRIDE, saving screenshot");
            kb->server->renderer->screenshot_queued = true;
            return;
        }
    }

    for (auto* resource : kb->resources) {
        if (!wroc_keyboard_resource_matches_focus_client(kb, resource)) continue;
        wl_keyboard_send_key(resource,
            wl_display_next_serial(kb->server->display),
            wroc_get_elapsed_milliseconds(kb->server),
            keycode, pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
    }
}

static
void wroc_keyboard_modifiers(wroc_keyboard* kb, u32 mods_depressed, u32 mods_latched, u32 mods_locked, u32 group)
{
    wroc_keyboard_update_active_modifiers(kb);

    for (auto* resource : kb->resources) {
        if (!wroc_keyboard_resource_matches_focus_client(kb, resource)) continue;
        wl_keyboard_send_modifiers(resource,
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
