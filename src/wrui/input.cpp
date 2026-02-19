#include "internal.hpp"

wrui_keyboard::~wrui_keyboard()
{
    xkb_keymap_unref(keymap);
    xkb_state_unref(state);
    xkb_context_unref(context);
}

auto wrui_keyboard_create(wrui_context*) -> ref<wrui_keyboard>
{
    auto kb = wrei_create<wrui_keyboard>();

    // Init XKB

    kb->context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    kb->keymap = xkb_keymap_new_from_names(kb->context, wrei_ptr_to(xkb_rule_names {
        .layout = "gb",
    }), XKB_KEYMAP_COMPILE_NO_FLAGS);

    kb->state = xkb_state_new(kb->keymap);

    // Get XKB modifier masks

    kb->mod_masks[wrui_modifier::shift] = xkb_keymap_mod_get_mask(kb->keymap, XKB_MOD_NAME_SHIFT);
    kb->mod_masks[wrui_modifier::ctrl]  = xkb_keymap_mod_get_mask(kb->keymap, XKB_MOD_NAME_CTRL);
    kb->mod_masks[wrui_modifier::caps]  = xkb_keymap_mod_get_mask(kb->keymap, XKB_MOD_NAME_CAPS);
    kb->mod_masks[wrui_modifier::super] = xkb_keymap_mod_get_mask(kb->keymap, XKB_VMOD_NAME_SUPER);
    kb->mod_masks[wrui_modifier::alt]   = xkb_keymap_mod_get_mask(kb->keymap, XKB_VMOD_NAME_ALT)
                                        | xkb_keymap_mod_get_mask(kb->keymap, XKB_VMOD_NAME_LEVEL3);
    kb->mod_masks[wrui_modifier::num]   = xkb_keymap_mod_get_mask(kb->keymap, XKB_VMOD_NAME_NUM);

    return kb;
}

static
xkb_keycode_t evdev_to_xkb(wrui_scancode code)
{
    return code + 8;
}

static
flags<xkb_state_component> handle_key(wrui_context* ctx, wrui_keyboard* kb, wrui_scancode code, bool pressed, bool quiet)
{
    if (pressed ? kb->pressed.inc(code) : kb->pressed.dec(code)) {

        char utf8[128] = {};
        if (pressed) xkb_state_key_get_utf8(kb->state, evdev_to_xkb(code), utf8, sizeof(utf8));
        auto sym = xkb_state_key_get_one_sym(kb->state, evdev_to_xkb(code));

        wrui_imgui_handle_key(ctx, sym, pressed, utf8);

        return xkb_state_update_key(kb->state, evdev_to_xkb(code), pressed ? XKB_KEY_DOWN : XKB_KEY_UP);
    }
    return {};
}

static
void update_leds(wrui_keyboard* kb)
{
    if (kb->led_devices.empty()) return;

    flags<libinput_led> leds = {};
    if (xkb_state_led_name_is_active(kb->state, XKB_LED_NAME_NUM)    > 0) leds |= LIBINPUT_LED_NUM_LOCK;
    if (xkb_state_led_name_is_active(kb->state, XKB_LED_NAME_CAPS)   > 0) leds |= LIBINPUT_LED_CAPS_LOCK;
    if (xkb_state_led_name_is_active(kb->state, XKB_LED_NAME_SCROLL) > 0) leds |= LIBINPUT_LED_SCROLL_LOCK;

    for (auto& device : kb->led_devices) {
        wrio_input_device_update_leds(device, leds);
    }
}

static
flags<wrui_modifier> get_modifiers(wrui_keyboard* kb, xkb_state_component component)
{
    flags<wrui_modifier> down = {};
    auto xkb_mods = xkb_state_serialize_mods(kb->state, component);
    for (auto mod : kb->mod_masks.enum_values) {
        if (xkb_mods & kb->mod_masks[mod]) down |= mod;
    }
    return down;
}

static
void handle_xkb_component_updates(wrui_context* ctx, wrui_keyboard* kb, flags<xkb_state_component> changed)
{
    if (changed & XKB_STATE_MODS_EFFECTIVE) {
        auto mods = get_modifiers(kb, XKB_STATE_MODS_EFFECTIVE);
        wrui_imgui_handle_mods(ctx, mods);
    }

    if (changed & XKB_STATE_LEDS) update_leds(kb);
}

// -----------------------------------------------------------------------------

auto wrui_pointer_create(wrui_context* ctx) -> ref<wrui_pointer>
{
    auto ptr = wrei_create<wrui_pointer>();

    auto scene = wrui_get_scene(ctx);

    ptr->transform = wrui_transform_create(ctx);
    wrui_node_set_transform(ptr->transform.get(), scene.transform);

    auto* cursor = XcursorLibraryLoadImage("default", "breeze_cursors", 24);
    defer { XcursorImageDestroy(cursor); };
    auto image = wren_image_create(ctx->wren, {cursor->width, cursor->height}, wren_format_from_drm(DRM_FORMAT_ABGR8888),
        wren_image_usage::texture | wren_image_usage::transfer);
    wren_image_update_immed(image.get(), cursor->pixels);

    ptr->visual = wrui_texture_create(ctx);
    wrui_texture_set_image(ptr->visual.get(), image.get(), ctx->render.sampler.get(), wren_blend_mode::premultiplied);
    wrui_texture_set_dst(ptr->visual.get(), {-vec2f32{cursor->xhot, cursor->yhot}, {cursor->width, cursor->height}, wrei_xywh});
    wrui_node_set_transform(ptr->visual.get(), ptr->transform.get());

    wrui_tree_place_above(scene.tree, nullptr, ptr->visual.get());

    return ptr;
}

static
void handle_button(wrui_context* ctx, wrui_pointer* ptr, wrui_scancode code, bool pressed, bool quiet)
{
    if (pressed ? ptr->pressed.inc(code) : ptr->pressed.dec(code)) {
        wrui_imgui_handle_button(ctx, code, pressed);
    }
}

static
void handle_motion(wrui_context* ctx, vec2f32 motion)
{
    auto ptr = ctx->pointer.get();
    auto cur = wrui_transform_get_local(ptr->transform.get());
    wrui_transform_update(ptr->transform.get(), cur.translation + motion, cur.scale);
    wrui_imgui_handle_motion(ctx);
}

static
void handle_scroll(wrui_context* ctx, vec2f32 delta)
{
    wrui_imgui_handle_wheel(ctx, delta);
}

// -----------------------------------------------------------------------------

void wrui_handle_input_added(wrui_context* ctx, wrio_input_device* device)
{
    if (wrio_input_device_get_capabilities(device).contains(wrio_input_device_capability::libinput_led)) {
        ctx->keyboard->led_devices.emplace_back(device);
    }
}

void wrui_handle_input_removed(wrui_context* ctx, wrio_input_device* device)
{
    std::erase(ctx->keyboard->led_devices, device);
}

void wrui_handle_input(wrui_context* ctx, const wrio_input_event& event)
{
    vec2f32 motion = {};
    vec2f32 scroll = {};
    flags<xkb_state_component> xkb_updates = {};

    for (auto& channel : event.channels) {
        switch (channel.type) {
            break;case EV_KEY:
                switch (channel.code) {
                    break;case BTN_MOUSE ... BTN_TASK:
                        handle_button(ctx, ctx->pointer.get(), channel.code, channel.value, event.quiet);
                    break;case KEY_ESC        ... KEY_MICMUTE:
                          case KEY_OK         ... KEY_LIGHTS_TOGGLE:
                          case KEY_ALS_TOGGLE ... KEY_PERFORMANCE:
                        xkb_updates |= handle_key(ctx, ctx->keyboard.get(), channel.code, channel.value, event.quiet);
                    break;default:
                        log_warn("Unknown  {} = {}", libevdev_event_code_get_name(channel.type, channel.code), channel.value);
                }
            break;case EV_REL:
                switch (channel.code) {
                    break;case REL_X: motion.x += channel.value;
                    break;case REL_Y: motion.y += channel.value;
                    break;case REL_HWHEEL: scroll.x += channel.value;
                    break;case REL_WHEEL:  scroll.y += channel.value;
                }
            break;case EV_ABS:
                log_warn("Unknown  {} = {}", libevdev_event_code_get_name(channel.type, channel.code), channel.value);
        }
    }

    if (motion.x || motion.y) handle_motion(ctx, motion);
    if (scroll.x || scroll.y) handle_scroll(ctx, scroll);

    if (xkb_updates) handle_xkb_component_updates(ctx, ctx->keyboard.get(), xkb_updates);
}
