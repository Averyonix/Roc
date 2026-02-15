#include "internal.hpp"

void wrio_input_device_add(wrio_input_device* device)
{
    wrei_assert(!std::ranges::contains(device->ctx->input_devices, device));
    device->ctx->input_devices.emplace_back(device);
    wrio_post_event(wrei_ptr_to(wrio_event {
        .ctx = device->ctx,
        .type = wrio_event_type::input_added,
        .input = wrio_input_event_data {
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
            .input = wrio_input_event_data {
                .device = device,
            },
        }));
    }
}

void wrio_input_device_leave(wrio_input_device* device)
{
    wrio_post_event(wrei_ptr_to(wrio_event {
        .ctx = device->ctx,
        .type = wrio_event_type::input_leave,
        .input = wrio_input_event_data {
            .device = device,
        },
    }));
}

void wrio_input_device_key_enter(wrio_input_device* device, std::span<const u32> keys)
{
    for (auto key : keys) {
        wrio_post_event(wrei_ptr_to(wrio_event {
            .ctx = device->ctx,
            .type = wrio_event_type::input_key_enter,
            .input = wrio_input_event_data {
                .device = device,
                .key = key,
            },
        }));
    }
}

void wrio_input_device_key_press(wrio_input_device* device, u32 key)
{
    wrio_post_event(wrei_ptr_to(wrio_event {
        .ctx = device->ctx,
        .type = wrio_event_type::input_key_press,
        .input = wrio_input_event_data {
            .device = device,
            .key = key,
        },
    }));
}

void wrio_input_device_key_release(wrio_input_device* device, u32 key)
{
    wrio_post_event(wrei_ptr_to(wrio_event {
        .ctx = device->ctx,
        .type = wrio_event_type::input_key_release,
        .input = wrio_input_event_data {
            .device = device,
            .key = key,
        },
    }));
}

void wrio_input_device_pointer_motion(wrio_input_device* device, vec2f64 delta)
{
    wrio_post_event(wrei_ptr_to(wrio_event {
        .ctx = device->ctx,
        .type = wrio_event_type::input_pointer_motion,
        .input = wrio_input_event_data {
            .device = device,
            .motion = delta,
        },
    }));
}

void wrio_input_device_pointer_axis(wrio_input_device* device, wrio_pointer_axis axis, f64 delta)
{
    wrio_post_event(wrei_ptr_to(wrio_event {
        .ctx = device->ctx,
        .type = wrio_event_type::input_pointer_axis,
        .input = wrio_input_event_data {
            .device = device,
            .axis = {
                .axis = axis,
                .delta = delta,
            },
        },
    }));
}
