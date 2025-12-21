#include "server.hpp"
#include "util.hpp"

#include "wrei/shm.hpp"

#include "wroc/event.hpp"

static void wroc_seat_keyboard_update_state(wroc_seat_keyboard*, wroc_key_action, std::span<const u32> actioned_keys);
static libinput_led wroc_seat_keyboard_get_leds(wroc_seat_keyboard*);

void wroc_keyboard::press(u32 keycode)
{
    if (!pressed.insert(keycode).second) return;

    wroc_seat_keyboard_update_state(target.get(), wroc_key_action::press, {keycode});
}

void wroc_keyboard::release(u32 keycode)
{
    if (!pressed.erase(keycode)) return;

    wroc_seat_keyboard_update_state(target.get(), wroc_key_action::release, {keycode});
}

void wroc_keyboard::enter(std::span<const u32> keycodes)
{
    std::vector<u32> filtered;

    for (auto& keycode : keycodes) {
        if (pressed.insert(keycode).second) {
            filtered.emplace_back(keycode);
        }
    }

    if (filtered.empty()) return;

    wroc_seat_keyboard_update_state(target.get(), wroc_key_action::enter, filtered);
}

void wroc_keyboard::leave()
{
    wroc_seat_keyboard_update_state(target.get(), wroc_key_action::release, pressed);

    pressed.clear();
}

wroc_keyboard::~wroc_keyboard()
{
    leave();

    if (target) {
        std::erase(target->sources, this);
    }
}

void wroc_seat_keyboard::attach(wroc_keyboard* kb)
{
    assert(!kb->target && "wroc_keyboard already attached to seat keyboard");

    sources.emplace_back(kb);
    kb->target = this;

    wroc_seat_keyboard_update_state(this, wroc_key_action::enter, kb->pressed);
    kb->update_leds(wroc_seat_keyboard_get_leds(this));
}

void wroc_seat_init_keyboard(wroc_seat* seat)
{
    seat->keyboard = wrei_create<wroc_seat_keyboard>();
    seat->keyboard->seat = seat;

    auto* kb = seat->keyboard.get();

    // Initialize XKB

    kb->context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    kb->keymap = xkb_keymap_new_from_names(kb->context, wrei_ptr_to(xkb_rule_names{
        .layout = "gb",
    }), XKB_KEYMAP_COMPILE_NO_FLAGS);

    kb->state = xkb_state_new(kb->keymap);

    // Get XKB modifier masks

    kb->mod_masks[wroc_modifiers::super] = xkb_keymap_mod_get_mask(kb->keymap, XKB_MOD_NAME_LOGO);
    kb->mod_masks[wroc_modifiers::shift] = xkb_keymap_mod_get_mask(kb->keymap, XKB_MOD_NAME_SHIFT);
    kb->mod_masks[wroc_modifiers::ctrl]  = xkb_keymap_mod_get_mask(kb->keymap, XKB_MOD_NAME_CTRL);
    kb->mod_masks[wroc_modifiers::alt]   = xkb_keymap_mod_get_mask(kb->keymap, XKB_MOD_NAME_ALT);
    kb->mod_masks[wroc_modifiers::num]   = xkb_keymap_mod_get_mask(kb->keymap, XKB_MOD_NAME_NUM);
    kb->mod_masks[wroc_modifiers::caps]  = xkb_keymap_mod_get_mask(kb->keymap, XKB_MOD_NAME_CAPS);

    // Create keymap file

    char* keymap_str = xkb_keymap_get_as_string(kb->keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
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

    // Start with numlock enabled

    kb->set_locked(wroc_modifiers::num, true);
}

wroc_seat_keyboard::~wroc_seat_keyboard()
{
    xkb_keymap_unref(keymap);
    xkb_state_unref(state);
    xkb_context_unref(context);
}

void wroc_seat_keyboard_on_get(wroc_seat_keyboard* kb, wl_client*, wl_resource* resource)
{
    wl_keyboard_send_keymap(resource, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, kb->keymap_fd, kb->keymap_size);

    if (wl_resource_get_version(resource) >= WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION) {
        wl_keyboard_send_repeat_info(resource, 25, 600);
    }
}

static
libinput_led wroc_seat_keyboard_get_leds(wroc_seat_keyboard* kb)
{
    int leds = 0;
    if (xkb_state_led_name_is_active(kb->state, XKB_LED_NAME_NUM))    leds |= LIBINPUT_LED_NUM_LOCK;
    if (xkb_state_led_name_is_active(kb->state, XKB_LED_NAME_CAPS))   leds |= LIBINPUT_LED_CAPS_LOCK;
    if (xkb_state_led_name_is_active(kb->state, XKB_LED_NAME_SCROLL)) leds |= LIBINPUT_LED_SCROLL_LOCK;

    return libinput_led(leds);
}

static
void wroc_seat_keyboard_update_leds(wroc_seat_keyboard* kb)
{
    auto leds = wroc_seat_keyboard_get_leds(kb);
    for (auto* source : kb->sources) {
        source->update_leds(leds);
    }
}

bool wroc_seat_keyboard::is_locked(wroc_modifiers mod) const
{
    auto locked = xkb_state_serialize_mods(state, XKB_STATE_MODS_LOCKED);
    return locked & (mod_masks[mod]);
}

WREI_DECORATE_FLAG_ENUM(xkb_state_component)

static
void wroc_seat_keyboard_handle_component_updates(wroc_seat_keyboard* kb, xkb_state_component changed_components)
{
    if (changed_components & (XKB_STATE_MODS_DEPRESSED | XKB_STATE_MODS_LATCHED | XKB_STATE_MODS_LOCKED | XKB_STATE_LAYOUT_EFFECTIVE)) {
        log_warn("Updated modifiers");
        wroc_post_event(kb->seat->server, wroc_keyboard_event {
            .type = wroc_event_type::keyboard_modifiers,
            .keyboard = kb,
            .mods {
                .depressed = xkb_state_serialize_mods(kb->state, XKB_STATE_MODS_DEPRESSED),
                .latched   = xkb_state_serialize_mods(kb->state, XKB_STATE_MODS_LATCHED),
                .locked    = xkb_state_serialize_mods(kb->state, XKB_STATE_MODS_LOCKED),
                .group     = xkb_state_serialize_mods(kb->state, XKB_STATE_LAYOUT_EFFECTIVE),
            }
        });
    }

    if (changed_components & XKB_STATE_LEDS) {
        log_warn("Updated LEDs");
        wroc_seat_keyboard_update_leds(kb);
    }
}

void wroc_seat_keyboard::set_locked(wroc_modifiers mod, bool locked)
{
    auto mask = mod_masks[mod];

    auto updated = xkb_state_update_latched_locked(state,
        0,    0,                 false, 0,
        mask, locked ? mask : 0, false, 0);
    wroc_seat_keyboard_handle_component_updates(this, updated);
}

static
void wroc_seat_keyboard_update_state(wroc_seat_keyboard* kb, wroc_key_action action, std::span<const u32> actioned_keys)
{
    xkb_state_component updated = {};

    for (auto key : actioned_keys) {
        if (action == wroc_key_action::release ? kb->pressed.dec(key) : kb->pressed.inc(key)) {
            if (action != wroc_key_action::enter) {
                wroc_post_event(kb->seat->server, wroc_keyboard_event {
                    .type = wroc_event_type::keyboard_key,
                    .keyboard = kb,
                    .key = { .keycode = key, .pressed = action == wroc_key_action::press },
                });
            }
        }

        updated |= xkb_state_update_key(kb->state, key + 8, action == wroc_key_action::release ?  XKB_KEY_UP : XKB_KEY_DOWN);
    }

    wroc_seat_keyboard_handle_component_updates(kb, updated);
}

wroc_modifiers wroc_keyboard_get_active_modifiers(wroc_seat_keyboard* kb)
{
    wroc_modifiers down = {};

    auto xkb_mods = xkb_state_serialize_mods(kb->state, XKB_STATE_MODS_EFFECTIVE);
    for (auto mod : kb->mod_masks.enum_values) {
        if (xkb_mods & kb->mod_masks[mod]) down |= mod;
    }

    if (down >= kb->seat->server->main_mod) {
        down |= wroc_modifiers::mod;
    }

    return down;
}

wroc_modifiers wroc_get_active_modifiers(wroc_server* server)
{
    wroc_modifiers mods = {};
    if (server->seat->keyboard) {
        mods |= wroc_keyboard_get_active_modifiers(server->seat->keyboard.get());
    }
    return mods;
}

static
bool wroc_keyboard_resource_matches_focus_client(wroc_seat_keyboard* kb, wl_resource* resource)
{
    if (!kb->focused_surface) return false;
    if (!kb->focused_surface->resource) return false;
    return wroc_resource_get_client(resource) == wroc_resource_get_client(kb->focused_surface->resource);
}

void wroc_keyboard_clear_focus(wroc_seat_keyboard* kb)
{
    if (auto* surface = kb->focused_surface.get(); surface && surface->resource) {
        auto serial = wl_display_next_serial(kb->seat->server->display);

        for (auto* resource : kb->resources) {
            if (!wroc_keyboard_resource_matches_focus_client(kb, resource)) continue;
            for (auto keycode : kb->pressed) {
                wl_keyboard_send_key(resource,
                    serial,
                    wroc_get_elapsed_milliseconds(kb->seat->server),
                    u32(keycode), WL_KEYBOARD_KEY_STATE_RELEASED);
            }
            wl_keyboard_send_modifiers(resource, serial, 0, 0, 0, 0);
            wl_keyboard_send_leave(resource, serial, surface->resource);
        }

        kb->focused_surface = nullptr;
    }
}

void wroc_keyboard_enter(wroc_seat_keyboard* kb, wroc_surface* surface)
{
    if (surface == kb->focused_surface.get()) return;

    wroc_keyboard_clear_focus(kb);

    if (!surface->resource) return;

    // TODO: Consolidate "client seat" into a with client's wl_keyboard/wl_pointer handles
    //       To deduplicate code between this and `wroc_pointer`

    log_warn("KEYBOARD ENTERED");
    kb->focused_surface = surface;

    auto serial = wl_display_next_serial(kb->seat->server->display);

    for (auto* resource : kb->resources) {
        if (!wroc_keyboard_resource_matches_focus_client(kb, resource)) continue;

        wroc_data_manager_offer_selection(kb->seat->server, wroc_resource_get_client(resource));

        wl_keyboard_send_enter(resource,
            serial,
            surface->resource, wrei_ptr_to(wroc_to_wl_array<const u32>(kb->pressed)));

        wl_keyboard_send_modifiers(resource,
            serial,
            xkb_state_serialize_mods(kb->state, XKB_STATE_MODS_DEPRESSED),
            xkb_state_serialize_mods(kb->state, XKB_STATE_MODS_LATCHED),
            xkb_state_serialize_mods(kb->state, XKB_STATE_MODS_LOCKED),
            xkb_state_serialize_layout(kb->state, XKB_STATE_LAYOUT_EFFECTIVE));

        wroc_surface_raise(surface);

        break;
    }
}

static
void wroc_debug_print_key(wroc_seat_keyboard* kb, u32 libinput_keycode, bool pressed)
{
    u32 xkb_keycode = wroc_key_to_xkb(libinput_keycode);
    char name[128] = {};
    char _utf[128] = {};

    auto sym = xkb_state_key_get_one_sym(kb->state, xkb_keycode);
    xkb_keysym_get_name(sym, name, sizeof(name) - 1);

    xkb_state_key_get_utf8(kb->state, xkb_keycode, _utf, sizeof(_utf) - 1);
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
void wroc_keyboard_key(wroc_seat_keyboard* kb, u32 keycode, bool pressed)
{
    wroc_debug_print_key(kb, keycode, pressed);

    {
        // TODO: Proper compositor keybind handling

        u32 xkb_keycode = wroc_key_to_xkb(keycode);
        auto sym = xkb_state_key_get_one_sym(kb->state, xkb_keycode);
        if (sym == XKB_KEY_Print && pressed) {
            log_debug("PRINT OVERRIDE, saving screenshot");
            kb->seat->server->renderer->screenshot_queued = true;
            return;
        }
    }

    for (auto* resource : kb->resources) {
        if (!wroc_keyboard_resource_matches_focus_client(kb, resource)) continue;
        wl_keyboard_send_key(resource,
            wl_display_next_serial(kb->seat->server->display),
            wroc_get_elapsed_milliseconds(kb->seat->server),
            keycode, pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
    }
}

static
void wroc_keyboard_modifiers(wroc_seat_keyboard* kb, u32 mods_depressed, u32 mods_latched, u32 mods_locked, u32 group)
{
    for (auto* resource : kb->resources) {
        if (!wroc_keyboard_resource_matches_focus_client(kb, resource)) continue;
        wl_keyboard_send_modifiers(resource,
            wl_display_next_serial(kb->seat->server->display),
            mods_depressed,
            mods_latched,
            mods_locked,
            group);
    }
}

void wroc_handle_keyboard_event(wroc_server* server, const wroc_keyboard_event& event)
{
    switch (event.type) {
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
