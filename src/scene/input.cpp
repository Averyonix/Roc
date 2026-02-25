#include "internal.hpp"

scene_keyboard::~scene_keyboard()
{
    xkb_keymap_unref(keymap);
    xkb_state_unref(state);
    xkb_context_unref(context);
}

auto scene_keyboard_create(scene_context*) -> ref<scene_keyboard>
{
    auto kb = core_create<scene_keyboard>();

    // Init XKB

    kb->context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    kb->keymap = xkb_keymap_new_from_names(kb->context, ptr_to(xkb_rule_names {
        .layout = "gb",
    }), XKB_KEYMAP_COMPILE_NO_FLAGS);

    kb->state = xkb_state_new(kb->keymap);

    // Get XKB modifier masks

    kb->mod_masks[scene_modifier::shift] = xkb_keymap_mod_get_mask(kb->keymap, XKB_MOD_NAME_SHIFT);
    kb->mod_masks[scene_modifier::ctrl]  = xkb_keymap_mod_get_mask(kb->keymap, XKB_MOD_NAME_CTRL);
    kb->mod_masks[scene_modifier::caps]  = xkb_keymap_mod_get_mask(kb->keymap, XKB_MOD_NAME_CAPS);
    kb->mod_masks[scene_modifier::super] = xkb_keymap_mod_get_mask(kb->keymap, XKB_VMOD_NAME_SUPER);
    kb->mod_masks[scene_modifier::alt]   = xkb_keymap_mod_get_mask(kb->keymap, XKB_VMOD_NAME_ALT)
                                         | xkb_keymap_mod_get_mask(kb->keymap, XKB_VMOD_NAME_LEVEL3);
    kb->mod_masks[scene_modifier::num]   = xkb_keymap_mod_get_mask(kb->keymap, XKB_VMOD_NAME_NUM);

    return kb;
}

static
auto get_modifiers(scene_keyboard* kb, flags<xkb_state_component> component) -> flags<scene_modifier>
{
    flags<scene_modifier> down = {};
    auto xkb_mods = xkb_state_serialize_mods(kb->state, component.get());
    for (auto mod : kb->mod_masks.enum_values) {
        if (xkb_mods & kb->mod_masks[mod]) down |= mod;
    }
    return down;
}

auto scene_keyboard_get_modifiers(scene_context* ctx) -> flags<scene_modifier>
{
    auto* kb = ctx->keyboard.get();
    return kb->depressed | kb->latched | kb->locked;
}

static
bool try_send_hotkey(scene_context* ctx, scene_scancode code, bool pressed)
{
    if (pressed) {
        // Ignore LOCKED modifier state like CAPS and NUMLOCK, as hotkyes require an exact match.
        scene_hotkey hotkey {ctx->keyboard->depressed | ctx->keyboard->latched, code};
        auto iter = ctx->hotkey.registered.find(hotkey);
        if (iter != ctx->hotkey.registered.end()) {
            ctx->hotkey.pressed.insert({code, {hotkey.mod, iter->second}});
            scene_client_post_event(iter->second, ptr_to(scene_event {
                .type = scene_event_type::hotkey,
                .hotkey = {
                    .hotkey  = hotkey,
                    .pressed = true,
                }
            }));
            return true;
        }
    } else {
        auto iter = ctx->hotkey.pressed.find(code);
        if (iter != ctx->hotkey.pressed.end()) {
            auto[mods, client] = iter->second;
            ctx->hotkey.pressed.erase(code);
            scene_client_post_event(client, ptr_to(scene_event {
                .type = scene_event_type::hotkey,
                .hotkey = {
                    .hotkey  = {mods, code},
                    .pressed = false,
                }
            }));
            return true;
        }
    }

    return false;
}

static
xkb_keycode_t evdev_to_xkb(scene_scancode code)
{
    return code + 8;
}

static
flags<xkb_state_component> handle_key(scene_context* ctx, scene_keyboard* kb, scene_scancode code, bool pressed, bool quiet)
{
    if (pressed ? kb->pressed.inc(code) : kb->pressed.dec(code)) {
        auto changed_components = xkb_state_update_key(kb->state, evdev_to_xkb(code), pressed ? XKB_KEY_DOWN : XKB_KEY_UP);

        if (!try_send_hotkey(ctx, code, pressed)) {
            if (auto* client = ctx->keyboard->focus.client) {
                char utf8[128] = {};
                if (pressed) xkb_state_key_get_utf8(kb->state, evdev_to_xkb(code), utf8, sizeof(utf8));
                auto sym = xkb_state_key_get_one_sym(kb->state, evdev_to_xkb(code));
                scene_client_post_event(client, ptr_to(scene_event {
                    .type = scene_event_type::keyboard_key,
                    .key = {
                        .code = code,
                        .sym = sym,
                        .utf8 = utf8,
                        .pressed = pressed,
                        .quiet = quiet,
                    },
                }));
            }
        }

        return changed_components;
    }
    return {};
}

static
void update_leds(scene_keyboard* kb)
{
    if (kb->led_devices.empty()) return;

    flags<libinput_led> leds = {};
    if (xkb_state_led_name_is_active(kb->state, XKB_LED_NAME_NUM)    > 0) leds |= LIBINPUT_LED_NUM_LOCK;
    if (xkb_state_led_name_is_active(kb->state, XKB_LED_NAME_CAPS)   > 0) leds |= LIBINPUT_LED_CAPS_LOCK;
    if (xkb_state_led_name_is_active(kb->state, XKB_LED_NAME_SCROLL) > 0) leds |= LIBINPUT_LED_SCROLL_LOCK;

    for (auto& device : kb->led_devices) {
        io_input_device_update_leds(device, leds);
    }
}

static
void handle_xkb_component_updates(scene_context* ctx, scene_keyboard* kb, flags<xkb_state_component> changed)
{
    if (changed & XKB_STATE_MODS_DEPRESSED) kb->depressed = get_modifiers(kb, XKB_STATE_MODS_DEPRESSED);
    if (changed & XKB_STATE_MODS_LATCHED)   kb->depressed = get_modifiers(kb, XKB_STATE_MODS_LATCHED);
    if (changed & XKB_STATE_MODS_LOCKED)    kb->depressed = get_modifiers(kb, XKB_STATE_MODS_LOCKED);

    if (changed & XKB_STATE_MODS_EFFECTIVE) {
        if (ctx->keyboard->focus.client) {
            scene_client_post_event(ctx->keyboard->focus.client, ptr_to(scene_event {
                .type = scene_event_type::keyboard_modifier,
                .key = {},
            }));
        }
    }

    if (changed & XKB_STATE_LEDS) update_leds(kb);
}

static
void update_keyboard_focus(scene_keyboard* kb, scene_client* new_client)
{
    auto* old_client = kb->focus.client;

    kb->focus = { new_client };

    if (old_client == new_client) return;

    scene_event event {
        .type = scene_event_type::focus_keyboard,
        .focus = {
            .lost   = { old_client },
            .gained = { new_client },
        }
    };

    if (old_client) scene_client_post_event(old_client, &event);
    if (new_client) scene_client_post_event(new_client, &event);
}

void scene_keyboard_grab(scene_client* client)
{
    update_keyboard_focus(client->ctx->keyboard.get(), client);
}

void scene_keyboard_ungrab(scene_client* client)
{
    if (client == client->ctx->keyboard->focus.client) {
        update_keyboard_focus(client->ctx->keyboard.get(), nullptr);
        // TODO: Offer focus to next most recently used keyboard grab
    }
}

void scene_keyboard_clear_focus(scene_context* ctx)
{
    update_keyboard_focus(ctx->keyboard.get(), nullptr);
}

// -----------------------------------------------------------------------------

static
scene_input_plane* find_input_plane_at(scene_tree* tree, vec2f32 pos)
{
    for (auto& node : tree->children | std::views::reverse) {
        switch (node->type) {
            break;case scene_node_type::tree:
                if (auto* result = find_input_plane_at(static_cast<scene_tree*>(node), pos)) {
                    return result;
                }
            break;case scene_node_type::input_plane: {
                auto* plane = static_cast<scene_input_plane*>(node);
                auto& global = plane->transform->global;
                aabb2f32 bounds {
                    global.translation + plane->rect.min * global.scale,
                    global.translation + plane->rect.max * global.scale,
                    core_minmax,
                };
                if (core_aabb_contains(bounds, pos)) {
                    return plane;
                }
            }
            break;default:
                ;
        }
    }

    return nullptr;
}

static
auto get_pointer_focus_client(scene_pointer* ptr) -> scene_client*
{
    return ptr->grab ?: ptr->focus.client;
}

static
void update_pointer_focus(scene_context* ctx, vec2f32 pos)
{
    scene_focus old_focus = ctx->pointer->focus;
    scene_focus new_focus = {};

    if (ctx->pointer->grab) {
        new_focus.client = ctx->pointer->grab;
    } else if (auto* plane = find_input_plane_at(ctx->root_tree.get(), pos)) {
        new_focus.client = plane->client;
        new_focus.plane = plane;
    }

    if (old_focus.plane == new_focus.plane && old_focus.client == new_focus.client) {
        return;
    }

    ctx->pointer->focus = new_focus;

    scene_event event {
        .type = scene_event_type::focus_pointer,
        .focus {
            .lost   = old_focus,
            .gained = new_focus,
        }
    };

    if (old_focus.client) scene_client_post_event(old_focus.client, &event);
    if (new_focus.client) scene_client_post_event(new_focus.client, &event);
}

void scene_update_pointer_focus(scene_context* ctx)
{
    update_pointer_focus(ctx, scene_pointer_get_position(ctx));
}

auto scene_pointer_get_position(scene_context* ctx) -> vec2f32
{
    return ctx->pointer->transform->global.translation;
}

// -----------------------------------------------------------------------------

auto scene_pointer_create(scene_context* ctx) -> ref<scene_pointer>
{
    auto ptr = core_create<scene_pointer>();

    ptr->transform = scene_transform_create(ctx);
    scene_node_set_transform(ptr->transform.get(), scene_get_root_transform(ctx));

    auto* cursor = XcursorLibraryLoadImage("default", "breeze_cursors", 24);
    defer { XcursorImageDestroy(cursor); };
    auto image = gpu_image_create(ctx->gpu, {cursor->width, cursor->height}, gpu_format_from_drm(DRM_FORMAT_ABGR8888),
        gpu_image_usage::texture | gpu_image_usage::transfer);
    gpu_image_update_immed(image.get(), cursor->pixels);

    ptr->visual = scene_texture_create(ctx);
    scene_texture_set_image(ptr->visual.get(), image.get(), ctx->render.sampler.get(), gpu_blend_mode::premultiplied);
    scene_texture_set_dst(ptr->visual.get(), {-vec2f32{cursor->xhot, cursor->yhot}, {cursor->width, cursor->height}, core_xywh});
    scene_node_set_transform(ptr->visual.get(), ptr->transform.get());

    scene_tree_place_above(scene_get_layer(ctx, scene_layer::overlay), nullptr, ptr->visual.get());

    return ptr;
}

static
void handle_button(scene_context* ctx, scene_pointer* ptr, scene_scancode code, bool pressed, bool quiet)
{
    if (pressed ? ptr->pressed.inc(code) : ptr->pressed.dec(code)) {
        if (!try_send_hotkey(ctx, code, pressed)) {
            if (auto* focus = get_pointer_focus_client(ctx->pointer.get())) {
                scene_client_post_event(focus, ptr_to(scene_event {
                    .type = scene_event_type::pointer_button,
                    .pointer = {
                        .button = {
                            .code    = code,
                            .pressed = pressed,
                            .quiet   = quiet,
                        }
                    },
                }));
            }
        }
    }
}

static
void handle_motion(scene_context* ctx, vec2f32 delta)
{
    auto ptr = ctx->pointer.get();
    auto cur = scene_transform_get_global(ptr->transform.get());

    auto res = ctx->pointer->driver({
        .position = cur.translation,
        .delta    = delta,
    });

    scene_transform_update(ptr->transform.get(), res.position, scene_transform_get_local(ptr->transform.get()).scale);

    update_pointer_focus(ctx, res.position);

    if (auto* focus = get_pointer_focus_client(ctx->pointer.get())) {
        scene_client_post_event(focus, ptr_to(scene_event {
            .type = scene_event_type::pointer_motion,
            .pointer = {
                .motion = {
                    .rel_accel   = res.accel,
                    .rel_unaccel = res.unaccel,
                },
            },
        }));
    }
}

static
void handle_scroll(scene_context* ctx, vec2f32 delta)
{
    if (auto* focus = get_pointer_focus_client(ctx->pointer.get())) {
        scene_client_post_event(focus, ptr_to(scene_event {
            .type = scene_event_type::pointer_scroll,
            .pointer = {
                .scroll = {
                    .delta = delta,
                }
            },
        }));
    }
}

void scene_pointer_grab(scene_client* client)
{
    auto* ctx = client->ctx;
    ctx->pointer->grab = client;
    update_pointer_focus(ctx, scene_pointer_get_position(ctx));
}

void scene_pointer_ungrab(scene_client* client)
{
    auto* ctx = client->ctx;
    if (ctx->pointer->grab == client) {
        ctx->pointer->grab = nullptr;
        update_pointer_focus(ctx, scene_pointer_get_position(ctx));
    }
}

void scene_pointer_set_driver(scene_context* ctx, std::move_only_function<scene_pointer_driver_fn>&& driver)
{
    ctx->pointer->driver = std::move(driver);
    handle_motion(ctx, {});
}

// -----------------------------------------------------------------------------

void scene_handle_input_added(scene_context* ctx, io_input_device* device)
{
    if (io_input_device_get_capabilities(device).contains(io_input_device_capability::libinput_led)) {
        ctx->keyboard->led_devices.emplace_back(device);
    }
}

void scene_handle_input_removed(scene_context* ctx, io_input_device* device)
{
    std::erase(ctx->keyboard->led_devices, device);
}

void scene_handle_input(scene_context* ctx, const io_input_event& event)
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

// -----------------------------------------------------------------------------

auto scene_client_hotkey_register(scene_client* client, scene_hotkey hotkey) -> bool
{
    auto& slot = client->ctx->hotkey.registered[hotkey];
    return !slot && (slot = client);
}

void scene_client_hotkey_unregister(scene_client* client, scene_hotkey hotkey)
{
    auto* ctx = client->ctx;
    auto iter = ctx->hotkey.registered.find(hotkey);
    if (iter == ctx->hotkey.registered.end()) return;
    if (iter->second != client) return;
    ctx->hotkey.registered.erase(hotkey);
    ctx->hotkey.pressed.erase(hotkey.code);
}

void scene_client_hotkey_unregister_all(scene_client* client)
{
    std::erase_if(client->ctx->hotkey.registered, [&](auto& e) {
        if (e.second == client) {
            client->ctx->hotkey.pressed.erase(e.first.code);
            return true;
        }
        return false;
    });
}
