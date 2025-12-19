#include "backend.hpp"
#include <wroc/event.hpp>

// -----------------------------------------------------------------------------

wroc_input_device::~wroc_input_device()
{
    libinput_device_unref(handle);
}

// -----------------------------------------------------------------------------

static
void device_init_keyboard(wroc_input_device* device)
{
    auto* server = device->backend->server;

    device->keyboard = wrei_create<wroc_libinput_keyboard>();
    device->keyboard->server = server;
    device->keyboard->base = device;

    server->seat->keyboard->attach(device->keyboard.get());
}

static
void handle_keyboard_key(wroc_input_device* device, libinput_event_keyboard* event)
{
    auto keycode = libinput_event_keyboard_get_key(event);

    switch (libinput_event_keyboard_get_key_state(event)) {
        break;case LIBINPUT_KEY_STATE_PRESSED:
            if (keycode == KEY_PAUSE) {
                log_error("PAUSE HIT - EMERGENCY SHUTDOWN");
                std::terminate();
            }
            device->keyboard->press(keycode);
        break;case LIBINPUT_KEY_STATE_RELEASED:
            device->keyboard->release(keycode);
    }
}

void wroc_libinput_keyboard::update_leds(libinput_led leds)
{
    libinput_device_led_update(base->handle, leds);
}

// -----------------------------------------------------------------------------

static
void device_init_pointer(wroc_input_device* device)
{
    device->pointer = wrei_create<wroc_libinput_pointer>();
    device->pointer->server = device->backend->server;
    device->pointer->base = device;
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

    auto device = wrei_create<wroc_input_device>();
    device->backend = backend;
    device->handle = libinput_device_ref(libinput_device);
    libinput_device_set_user_data(libinput_device, device.get());

    bool want_device = false;
    if (libinput_device_has_capability(libinput_device, LIBINPUT_DEVICE_CAP_KEYBOARD)) {
        log_debug("  has keyboard capability");
        device_init_keyboard(device.get());
        want_device = true;
    }

    if (libinput_device_has_capability(libinput_device, LIBINPUT_DEVICE_CAP_POINTER)) {
        log_debug("  has pointer capability");
        if ("Glorious Model O"sv == name) {
            device_init_pointer(device.get());
            want_device = true;
        } else {
            log_warn("  TODO: Support multiple pointers");
        }
    }

    if (want_device) {
        backend->input_devices.emplace_back(device);
    }
}

static
void handle_device_removed(wroc_input_device* device)
{
    log_warn("Device removed - {}", libinput_device_get_name(device->handle));

    std::erase_if(device->backend->input_devices, [&](const auto& d) { return d.get() == device; });
}

// -----------------------------------------------------------------------------

void wroc_backend_handle_libinput_event(wroc_direct_backend* backend, libinput_event* event)
{
    // auto type = libinput_event_get_type(event);
    // log_debug("libinput event: {}", magic_enum::enum_name(type));

    auto event_type = libinput_event_get_type(event);
    if (event_type == LIBINPUT_EVENT_NONE) return;

    auto libinput_dev = libinput_event_get_device(event);
    auto device = static_cast<wroc_input_device*>(libinput_device_get_user_data(libinput_dev));
    if (!device && event_type != LIBINPUT_EVENT_DEVICE_ADDED) {
        log_error("libinput event has no associated device");
        return;
    }

    switch (event_type) {
        break;case LIBINPUT_EVENT_DEVICE_ADDED:   handle_device_added(backend, libinput_dev);
        break;case LIBINPUT_EVENT_DEVICE_REMOVED: handle_device_removed(device);
        break;case LIBINPUT_EVENT_KEYBOARD_KEY:   handle_keyboard_key(device, libinput_event_get_keyboard_event(event));
        break;case LIBINPUT_EVENT_POINTER_BUTTON: handle_pointer_button(device, libinput_event_get_pointer_event(event));
        break;case LIBINPUT_EVENT_POINTER_MOTION: handle_pointer_motion(device, libinput_event_get_pointer_event(event));
        break;case LIBINPUT_EVENT_POINTER_AXIS:   /* ignored */
        break;case LIBINPUT_EVENT_POINTER_SCROLL_WHEEL: handle_pointer_scroll_wheel(device, libinput_event_get_pointer_event(event));
        break;default:
            log_warn("unhandled libinput event: {} ({})", magic_enum::enum_name(event_type), u32(event_type));
    }
}
