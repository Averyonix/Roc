#include "backend.hpp"
#include <wroc/event.hpp>

// -----------------------------------------------------------------------------

static
void device_init_keyboard(wroc_input_device* device)
{
    device->keyboard = wrei_create<wroc_libinput_keyboard>();
    device->keyboard->server = device->backend->server;

    device->keyboard->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    wroc_post_event(device->backend->server, wroc_keyboard_event {
        .type = wroc_event_type::keyboard_added,
        .keyboard = device->keyboard.get(),
    });

    // keymap

    auto* kb = device->keyboard.get();

    xkb_keymap* keymap = xkb_keymap_new_from_names(kb->xkb_context, wrei_ptr_to(xkb_rule_names{
        .layout = "gb",
    }), XKB_KEYMAP_COMPILE_NO_FLAGS);
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

// TODO: factor this out
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
void update_keyboard_modifiers(wroc_libinput_keyboard* kb, u32 keycode, bool pressed)
{
    u32 xkb_keycode = wroc_key_to_xkb(keycode);
    auto sym = xkb_state_key_get_one_sym(kb->xkb_state, xkb_keycode);

    bool update_mods = false;
    auto mods_depressed = xkb_state_serialize_mods(kb->xkb_state, XKB_STATE_MODS_DEPRESSED);
    auto mods_locked = xkb_state_serialize_mods(kb->xkb_state, XKB_STATE_MODS_LOCKED);
    auto mods_latched = xkb_state_serialize_mods(kb->xkb_state, XKB_STATE_MODS_LATCHED);
    auto group = xkb_state_serialize_mods(kb->xkb_state, XKB_STATE_LAYOUT_EFFECTIVE);
    for (auto[i, mod] : wroc_modifier_info | std::views::enumerate) {
        auto mask = kb->xkb_mod_masks[i].second;

        if (sym == mod.left || sym == mod.right) {
            log_warn("Mod left|right {} (mask = {:#x})", pressed ? "pressed" : "released", mask);
            auto depressed = std::ranges::find_if(kb->pressed, [&](auto k) {
                auto s = xkb_state_key_get_one_sym(kb->xkb_state, wroc_key_to_xkb(k));
                return s == mod.left || s == mod.right;
            }) != kb->pressed.end();
            log_warn("  any down: {}", depressed);
            log_warn("  mods_depressed before: {:#b}", mods_depressed);
            mods_depressed = (mods_depressed & ~mask) | (depressed ? mask : 0);
            log_warn("  mods_depressed after:  {:#b}", mods_depressed);
            update_mods = true;
        }

        if (sym == mod.lock && pressed) {
            log_warn("Mod lock pressed (mask = {:#x})", mask);
            auto locked = mods_locked & mask;
            mods_locked = (mods_locked & ~mask) | (!locked ? mask : 0);
            update_mods = true;
        }
    }
    if (update_mods) {
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
}

static
void handle_keyboard_key(wroc_input_device* device, libinput_event_keyboard* event)
{
    auto keycode = libinput_event_keyboard_get_key(event);
    auto state = libinput_event_keyboard_get_key_state(event);
    bool pressed = state == LIBINPUT_KEY_STATE_PRESSED;

    auto* kb = device->keyboard.get();

    update_kb_key_state(kb, keycode, pressed);

    update_keyboard_modifiers(kb, keycode, pressed);

    wroc_post_event(kb->server, wroc_keyboard_event {
        .type = wroc_event_type::keyboard_key,
        .keyboard = kb,
        .key { .keycode = keycode, .pressed = pressed },
    });
}

wroc_input_device::~wroc_input_device()
{
    libinput_device_unref(handle);
}

// -----------------------------------------------------------------------------

static
void device_init_pointer(wroc_input_device* device)
{
    device->pointer = wrei_create<wroc_libinput_pointer>();
    device->pointer->server = device->backend->server;
    if (device->backend->outputs.empty()) {
        log_error("NO OUTPUTS FOUND!");
    }
    device->pointer->current_output = device->backend->outputs.front().get();

    if (libinput_device_config_accel_is_available(device->handle)) {
        libinput_device_config_accel_set_profile(device->handle, LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT);
        libinput_device_config_accel_set_speed(  device->handle, -0.6);
    }

    auto* pointer = device->pointer.get();

    wroc_post_event(device->backend->server, wroc_pointer_event {
        .type = wroc_event_type::pointer_added,
        .pointer = pointer,
    });

    // TODO: Generic cursor state management
    auto* output = device->backend->outputs.front().get();
    pointer->layout_position = vec2f64(output->size) / 2.0;
    wroc_post_event(pointer->server, wroc_pointer_event {
        .type = wroc_event_type::pointer_motion,
        .pointer = pointer,
        .output = output,
        .motion = {},
    });
}

// TODO: Factor me out!
static
void update_pointer_button_state(wroc_pointer* pointer, u32 button, bool state)
{
    if (!state) {
        std::erase(pointer->pressed, button);
    } else if (std::ranges::find(pointer->pressed, button) == pointer->pressed.end()) {
        pointer->pressed.emplace_back(button);
    }
}

static
void handle_pointer_button(wroc_input_device* device, libinput_event_pointer* event)
{
    auto* pointer = device->pointer.get();

    auto button = libinput_event_pointer_get_button(event);
    auto state = libinput_event_pointer_get_button_state(event);

    update_pointer_button_state(pointer, button, state == LIBINPUT_BUTTON_STATE_PRESSED);

    wroc_post_event(pointer->server, wroc_pointer_event {
        .type = wroc_event_type::pointer_button,
        .pointer = pointer,
        .output = pointer->current_output,
        .button = { .button = button, .pressed = state == LIBINPUT_BUTTON_STATE_PRESSED },
    });
}

static
void handle_pointer_motion(wroc_input_device* device, libinput_event_pointer* event)
{
    auto* pointer = device->pointer.get();

    vec2f64 delta = {libinput_event_pointer_get_dx(event), libinput_event_pointer_get_dy(event)};

    // TODO: Generic cursor state management
    auto* output = pointer->current_output;
    auto pos = device->pointer->layout_position + delta;
    if (pos.x < 0) pos.x = 0;
    if (pos.y < 0) pos.y = 0;
    if (pos.x >= output->size.x) pos.x = output->size.x - 1;
    if (pos.y >= output->size.y) pos.y = output->size.y - 1;

    // log_warn("motion ({}, {}) - ({}, {}) -> ({}, {})",
    //     delta.x, delta.y,
    //     device->pointer->layout_position.x, device->pointer->layout_position.y,
    //     pos.x, pos.y);

    device->pointer->layout_position = pos;

    wroc_post_event(pointer->server, wroc_pointer_event {
        .type = wroc_event_type::pointer_motion,
        .pointer = pointer,
        .output = pointer->current_output,
        .motion = {},
    });
}

static
void handle_pointer_scroll_wheel(wroc_input_device* device, libinput_event_pointer* event)
{
    auto get = [&](libinput_pointer_axis axis) {
        return libinput_event_pointer_has_axis(event, axis)
            ? libinput_event_pointer_get_scroll_value_v120(event, axis) / 120.0
            : 0.0;
    };

    double dx = get(LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);
    double dy = get(LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);

    auto* pointer = device->pointer.get();
    wroc_post_event(pointer->server, wroc_pointer_event {
        .type = wroc_event_type::pointer_axis,
        .pointer = pointer,
        .output = pointer->current_output,
        .axis {
            .delta = { dx, dy },
        },
    });
}

// -----------------------------------------------------------------------------

static
void handle_device_added(wroc_direct_backend* backend, struct libinput_device* libinput_device)
{
    int vendor = libinput_device_get_id_vendor(libinput_device);
    int product = libinput_device_get_id_product(libinput_device);
    const char* name = libinput_device_get_name(libinput_device);

    log_debug("Adding {} [{}:{}]", name, vendor, product);

    if ("Wooting Wooting Two HE (ARM)"sv != name
            && "Glorious Model O"sv != name) {
        log_warn("Support multiple keyboards/pointers");
        return;
    }

    auto device = wrei_create<wroc_input_device>();
    device->backend = backend;
    device->handle = libinput_device_ref(libinput_device);
    libinput_device_set_user_data(libinput_device, device.get());

    backend->input_devices.emplace_back(device);

    if (libinput_device_has_capability(libinput_device, LIBINPUT_DEVICE_CAP_KEYBOARD)) {
        log_debug("  has keyboard capability");
        device_init_keyboard(device.get());
    }

    if (libinput_device_has_capability(libinput_device, LIBINPUT_DEVICE_CAP_POINTER)) {
        log_debug("  has pointer capability");
        device_init_pointer(device.get());
    }
}

// -----------------------------------------------------------------------------

void wroc_backend_handle_libinput_event(wroc_direct_backend* backend, libinput_event* event)
{
    // auto type = libinput_event_get_type(event);
    // log_debug("libinput event: {}", magic_enum::enum_name(type));

    auto event_type = libinput_event_get_type(event);
    if (event_type == LIBINPUT_EVENT_NONE) return;

    auto libinput_dev = libinput_event_get_device(event);
    auto dev = static_cast<wroc_input_device*>(libinput_device_get_user_data(libinput_dev));
    if (!dev && event_type != LIBINPUT_EVENT_DEVICE_ADDED) {
        log_error("libinput event has no associated device");
        return;
    }

    switch (event_type) {
        break;case LIBINPUT_EVENT_DEVICE_ADDED:
            handle_device_added(backend, libinput_dev);
        break;case LIBINPUT_EVENT_KEYBOARD_KEY:
            {
                auto* kb_event = libinput_event_get_keyboard_event(event);
                auto key = libinput_event_keyboard_get_key(kb_event);
                if (key == KEY_PAUSE && libinput_event_keyboard_get_key_state(kb_event) == LIBINPUT_KEY_STATE_PRESSED) {
                    log_error("PAUSE HIT EMERGENCY SHUTDOWN");
                    std::terminate();
                }
            }
            handle_keyboard_key(dev, libinput_event_get_keyboard_event(event));
        break;case LIBINPUT_EVENT_POINTER_BUTTON:
            handle_pointer_button(dev, libinput_event_get_pointer_event(event));
        break;case LIBINPUT_EVENT_POINTER_MOTION:
            handle_pointer_motion(dev, libinput_event_get_pointer_event(event));
        break;case LIBINPUT_EVENT_POINTER_AXIS:
            ;
        break;case LIBINPUT_EVENT_POINTER_SCROLL_WHEEL:
            handle_pointer_scroll_wheel(dev, libinput_event_get_pointer_event(event));
        break;default:
            log_warn("unhandled libinput event: {} ({})", magic_enum::enum_name(event_type), u32(event_type));
    }
}
