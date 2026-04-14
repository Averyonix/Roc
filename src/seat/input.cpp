#include "internal.hpp"

#include <core/math.hpp>

// -----------------------------------------------------------------------------

SeatKeyboard::~SeatKeyboard()
{
    xkb_keymap_unref(keymap);
    xkb_state_unref(state);
    xkb_context_unref(context);
}

auto seat_keyboard_create(const SeatKeyboardCreateInfo& info) -> Ref<SeatKeyboard>
{
    auto keyboard = ref_create<SeatKeyboard>();

    keyboard->rate = info.rate;
    keyboard->delay = info.delay;

    // Init XKB

    keyboard->context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    keyboard->keymap = xkb_keymap_new_from_names(keyboard->context, ptr_to(xkb_rule_names {
        .layout = info.layout,
    }), XKB_KEYMAP_COMPILE_NO_FLAGS);

    keyboard->state = xkb_state_new(keyboard->keymap);

    // Get XKB modifier masks

    keyboard->mod_masks[SeatModifier::shift] = xkb_keymap_mod_get_mask(keyboard->keymap, XKB_MOD_NAME_SHIFT);
    keyboard->mod_masks[SeatModifier::ctrl]  = xkb_keymap_mod_get_mask(keyboard->keymap, XKB_MOD_NAME_CTRL);
    keyboard->mod_masks[SeatModifier::caps]  = xkb_keymap_mod_get_mask(keyboard->keymap, XKB_MOD_NAME_CAPS);
    keyboard->mod_masks[SeatModifier::super] = xkb_keymap_mod_get_mask(keyboard->keymap, XKB_VMOD_NAME_SUPER);
    keyboard->mod_masks[SeatModifier::alt]   = xkb_keymap_mod_get_mask(keyboard->keymap, XKB_VMOD_NAME_ALT)
                                             | xkb_keymap_mod_get_mask(keyboard->keymap, XKB_VMOD_NAME_LEVEL3);
    keyboard->mod_masks[SeatModifier::num]   = xkb_keymap_mod_get_mask(keyboard->keymap, XKB_VMOD_NAME_NUM);

    return keyboard;
}

static
auto get_modifiers(SeatKeyboard* keyboard, Flags<xkb_state_component> component) -> Flags<SeatModifier>
{
    Flags<SeatModifier> down = {};
    auto xkb_mods = xkb_state_serialize_mods(keyboard->state, component.get());
    for (auto mod : keyboard->mod_masks.enum_values) {
        if (xkb_mods & keyboard->mod_masks[mod]) down |= mod;
    }
    return down;
}

auto seat_keyboard_get_modifiers(SeatKeyboard* keyboard, Flags<SeatModifierFlag> flags) -> Flags<SeatModifier>
{
    auto mods = keyboard->depressed | keyboard->latched;
    if (!flags.contains(SeatModifierFlag::ignore_locked)) mods |= keyboard->locked;
    return mods;
}

auto seat_get_modifiers(Seat* seat, Flags<SeatModifierFlag> flags) -> Flags<SeatModifier>
{
    return seat_keyboard_get_modifiers(seat_get_keyboard(seat), flags);
}

static
xkb_keycode_t evdev_to_xkb(SeatInputCode code)
{
    return code + 8;
}

static
bool post_input_event(Weak<SeatInputDevice> device, SeatEvent* event)
{
    for (auto* filter : device->seat->input_event_filters) {
        if (filter->filter(event) == SeatEventFilterResult::capture) {
            return false;
        }
    }

    if (!device) return false;
    if (device->focus) {
        seat_client_post_event(device->focus->client, event);
        return true;
    }

    return false;
}

auto seat_keyboard_get_leds(SeatKeyboard* keyboard) -> Flags<libinput_led>
{
    Flags<libinput_led> leds = {};
    if (xkb_state_led_name_is_active(keyboard->state, XKB_LED_NAME_NUM)    > 0) leds |= LIBINPUT_LED_NUM_LOCK;
    if (xkb_state_led_name_is_active(keyboard->state, XKB_LED_NAME_CAPS)   > 0) leds |= LIBINPUT_LED_CAPS_LOCK;
    if (xkb_state_led_name_is_active(keyboard->state, XKB_LED_NAME_SCROLL) > 0) leds |= LIBINPUT_LED_SCROLL_LOCK;
    return leds;
}

static
void handle_xkb_component_updates(SeatKeyboard* keyboard, Flags<xkb_state_component> changed)
{
    if (changed & XKB_STATE_MODS_DEPRESSED) keyboard->depressed = get_modifiers(keyboard, XKB_STATE_MODS_DEPRESSED);
    if (changed & XKB_STATE_MODS_LATCHED)   keyboard->depressed = get_modifiers(keyboard, XKB_STATE_MODS_LATCHED);
    if (changed & XKB_STATE_MODS_LOCKED)    keyboard->depressed = get_modifiers(keyboard, XKB_STATE_MODS_LOCKED);

    if (changed & XKB_STATE_MODS_EFFECTIVE) {
        post_input_event(keyboard, ptr_to(SeatEvent {
            .keyboard = {
                .type = SeatEventType::keyboard_modifier,
                .keyboard = keyboard,
            },
        }));
    }
}

auto seat_keyboard_key(SeatKeyboard* keyboard, SeatInputCode code, bool pressed, bool quiet) -> Flags<xkb_state_component>
{
    Flags<xkb_state_component> changed = {};

    if (pressed ? keyboard->pressed.inc(code) : keyboard->pressed.dec(code)) {
        // TODO: Query symbol *before* update
        changed = xkb_state_update_key(keyboard->state, evdev_to_xkb(code), pressed ? XKB_KEY_DOWN : XKB_KEY_UP);

        post_input_event(keyboard, ptr_to(SeatEvent {
            .keyboard = {
                .type = SeatEventType::keyboard_key,
                .keyboard = keyboard,
                .key = {
                    .code = code,
                    .pressed = pressed,
                    .quiet = quiet,
                },
            }
        }));

        handle_xkb_component_updates(keyboard, changed);
    }

    return changed;
}

void seat_keyboard_focus(SeatKeyboard* keyboard, SeatInputRegion* new_focus)
{
    auto* old_focus = seat_keyboard_get_focus(keyboard);

    keyboard->focus = new_focus;

    if (old_focus == new_focus) return;

    auto old_client = seat_get_focus_client(old_focus);
    auto new_client = seat_get_focus_client(new_focus);

    if (old_client && (old_client != new_client)) {
        seat_client_post_event(old_client, ptr_to(SeatEvent {
            .keyboard = {
                .type = SeatEventType::keyboard_leave,
                .keyboard = keyboard,
            },
        }));
    }

    if (new_focus) {
        seat_client_post_event(new_client, ptr_to(SeatEvent {
            .keyboard = {
                .type = SeatEventType::keyboard_enter,
                .keyboard = keyboard,
                .focus = new_focus,
            },
        }));
    }
}

auto seat_keyboard_get_focus(SeatKeyboard* keyboard) -> SeatInputRegion*
{
    return keyboard->focus.get();
}

auto seat_keyboard_get_seat(SeatKeyboard* keyboard) -> Seat*
{
    return keyboard->seat;
}

auto seat_keyboard_get_pressed(SeatKeyboard* keyboard) -> std::span<const SeatInputCode>
{
    return keyboard->pressed;
}

auto seat_keyboard_get_sym(SeatKeyboard* keyboard, SeatInputCode code) -> xkb_keysym_t
{
    return xkb_state_key_get_one_sym(keyboard->state, evdev_to_xkb(code));
}

auto seat_keyboard_get_utf8(SeatKeyboard* keyboard, SeatInputCode code) -> std::string
{
    std::string utf8;
    utf8.resize(xkb_state_key_get_utf8(keyboard->state, evdev_to_xkb(code), nullptr, 0));
    xkb_state_key_get_utf8(keyboard->state, evdev_to_xkb(code), utf8.data(), utf8.size() + 1);
    return utf8;
}

auto seat_keyboard_get_info(SeatKeyboard* keyboard) -> const SeatKeyboardInfo&
{
    return *keyboard;
}

// -----------------------------------------------------------------------------

auto seat_find_input_region_at(SceneTree* tree, vec2f32 pos) -> SeatInputRegion*
{
    SeatInputRegion* region = nullptr;

    scene_iterate<SceneIterateDirection::front_to_back>(tree,
        scene_iterate_default,
        [&](SceneNode* node) {
            if (auto input_region = dynamic_cast<SeatInputRegion*>(node)) {
                if (input_region->region.contains(pos - scene_tree_get_position(input_region->parent))) {
                    region = input_region;
                    return SceneIterateAction::stop;
                }
            }
            return SceneIterateAction::next;
        },
        scene_iterate_default);

    return region;
}

void seat_pointer_focus(SeatPointer* pointer, SeatInputRegion* new_focus)
{
    auto* old_focus = seat_pointer_get_focus(pointer);

    if (old_focus == new_focus) return;

    auto old_client = seat_get_focus_client(old_focus);
    auto new_client = seat_get_focus_client(new_focus);

    pointer->focus = new_focus;

    if (old_client && old_client != new_client) {
        seat_client_post_event(old_client, ptr_to(SeatEvent {
            .pointer = {
                .type = SeatEventType::pointer_leave,
                .pointer = pointer,
            },
        }));
    }

    if (new_client) {
        seat_client_post_event(new_client, ptr_to(SeatEvent {
            .pointer = {
                .type = SeatEventType::pointer_enter,
                .pointer = pointer,
                .focus= new_focus,
            }
        }));
    }

    if (!new_focus) {
        seat_pointer_set_xcursor(pointer, "default");
    }
}

static
void update_pointer_focus(SeatPointer* pointer)
{
    SeatInputRegion* new_focus = nullptr;

    if (!seat_pointer_get_pressed(pointer).empty()) {
        // Pointer retains old focus while any pointer buttons pressed
        new_focus = seat_pointer_get_focus(pointer);

    } else if (auto* region = seat_find_input_region_at(pointer->root, seat_pointer_get_position(pointer))) {
        new_focus = region;
    }

    seat_pointer_focus(pointer, new_focus);
}

auto seat_pointer_get_position(SeatPointer* pointer) -> vec2f32
{
    return scene_tree_get_position(pointer->tree.get());
}

auto seat_pointer_get_pressed(SeatPointer* pointer) -> std::span<const SeatInputCode>
{
    return pointer->pressed;
}

// -----------------------------------------------------------------------------

auto seat_pointer_create(const SeatPointerCreateInfo& info) -> Ref<SeatPointer>
{
    auto pointer = ref_create<SeatPointer>();
    pointer->cursor_manager = info.cursor_manager;
    pointer->root = info.root;

    pointer->tree = scene_tree_create();
    scene_tree_place_above(info.layer, nullptr, pointer->tree .get());

    return pointer;
}

void seat_pointer_button(SeatPointer* pointer, SeatInputCode code, bool pressed, bool quiet)
{
    if (pressed ? pointer->pressed.inc(code) : pointer->pressed.dec(code)) {
        if (post_input_event(pointer, ptr_to(SeatEvent {
            .pointer = {
                .type = SeatEventType::pointer_button,
                .pointer = pointer,
                .button = {
                    .code    = code,
                    .pressed = pressed,
                    .quiet   = quiet,
                },
        }}))) {
            if (pressed) {
                seat_keyboard_focus(
                    seat_get_keyboard(pointer->seat),
                    seat_pointer_get_focus(pointer));
            }
        } else if (pressed) {
            seat_keyboard_focus(seat_get_keyboard(pointer->seat), nullptr);
        }
        if (!pressed) {
            update_pointer_focus(pointer);
        }
    }
}

void seat_pointer_move(SeatPointer* pointer, vec2f32 position, vec2f32 rel_accel, vec2f32 rel_unaccel)
{
    bool send_event = pointer->tree->translation != position
                   || rel_accel.x   || rel_accel.y
                   || rel_unaccel.x || rel_unaccel.y;

    scene_tree_set_translation(pointer->tree.get(), position);

    update_pointer_focus(pointer);

    if (!send_event) return;

    post_input_event(pointer, ptr_to(SeatEvent {
        .pointer = {
            .type = SeatEventType::pointer_motion,
            .pointer = pointer,
            .motion = {
                .rel_accel   = rel_accel,
                .rel_unaccel = rel_unaccel,
            },
        },
    }));
}

void seat_pointer_scroll(SeatPointer* pointer, vec2f32 delta)
{
    post_input_event(pointer, ptr_to(SeatEvent {
        .pointer = {
            .type = SeatEventType::pointer_scroll,
            .pointer = pointer,
            .scroll = {
                .delta = delta,
            }
        },
    }));
}

auto seat_pointer_get_focus(SeatPointer* pointer) -> SeatInputRegion*
{
    return pointer->focus.get();
}

auto seat_pointer_get_seat(SeatPointer* pointer) -> Seat*
{
    return pointer->seat;
}
