#include "internal.hpp"

auto wrio_list_input_devices(wrio_context* ctx) -> std::span<wrio_input_device* const>
{
    return ctx->input_devices;
}

auto wrio_input_device_get_capabilities(wrio_input_device* device) -> flags<wrio_input_device_capability>
{
    return device->capabilities;
}

void wrio_input_device_update_leds(wrio_input_device* device, flags<libinput_led> leds)
{
    log_warn("TODO: Keyboard LEDs: [{}]", wrei_bitfield_to_string(leds.get()));
}

void wrio_input_device_add(wrio_input_device* device)
{
    wrei_assert(!std::ranges::contains(device->ctx->input_devices, device));
    device->ctx->input_devices.emplace_back(device);
    wrio_post_event(wrei_ptr_to(wrio_event {
        .ctx = device->ctx,
        .type = wrio_event_type::input_added,
        .input = wrio_input_event {
            .device = device,
        },
    }));
}

void wrio_input_device_remove(wrio_input_device* device)
{
    if (std::erase(device->ctx->input_devices, device)) {
        wrio_post_event(wrei_ptr_to(wrio_event {
            .ctx = device->ctx,
            .type = wrio_event_type::input_removed,
            .input = wrio_input_event {
                .device = device,
            },
        }));
    }
}

static
void post_input(wrio_input_device* device, bool quiet, std::span<const wrio_input_channel> channels)
{
    wrio_post_event(wrei_ptr_to(wrio_event {
        .ctx = device->ctx,
        .type = wrio_event_type::input_event,
        .input = wrio_input_event {
            .device = device,
            .quiet = quiet,
            .channels = channels,
        },
    }));
}

void wrio_input_device_leave(wrio_input_device* device)
{
    std::vector<wrio_input_channel> events;
    events.reserve(device->pressed.size());
    for (auto key : device->pressed) {
        events.emplace_back(EV_KEY, key, 0);
    }
    if (!events.empty()) {
        post_input(device, true, events);
    }
    device->pressed.clear();
}

void wrio_input_device_key_enter(wrio_input_device* device, std::span<const u32> keys)
{
    std::vector<wrio_input_channel> events;
    events.reserve(keys.size());
    for (auto key : keys) {
        if (device->pressed.insert(key).second) {
            events.emplace_back(EV_KEY, key, 1);
        }
    }
    if (!events.empty()) {
        post_input(device, true, events);
    }
}

void wrio_input_device_key_press(wrio_input_device* device, u32 key)
{
    if (device->pressed.insert(key).second) {
        post_input(device, false, {{EV_KEY, key, 1}});
    }
}

void wrio_input_device_key_release(wrio_input_device* device, u32 key)
{
    if (device->pressed.erase(key)) {
        post_input(device, false, {{EV_KEY, key, 0}});
    }
}

static
void post_rel2(wrio_input_device* device, vec2u32 code, vec2f32 delta)
{
    wrio_input_channel events[2];
    u32 count = 0;
    if (delta.x) events[count++] = {EV_REL, code.x, delta.x};
    if (delta.y) events[count++] = {EV_REL, code.y, delta.y};
    post_input(device, false, std::span(events, count));
}

void wrio_input_device_pointer_motion(wrio_input_device* device, vec2f32 delta)
{
    post_rel2(device, {REL_X, REL_Y}, delta);
}

void wrio_input_device_pointer_scroll(wrio_input_device* device, vec2f32 delta)
{
    post_rel2(device, {REL_HWHEEL, REL_WHEEL}, delta);
}
