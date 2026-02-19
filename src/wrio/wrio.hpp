#pragma once

#include "wrei/object.hpp"
#include "wrei/event.hpp"

#include "wren/wren.hpp"

// -----------------------------------------------------------------------------

struct wrio_context;
struct wrio_input_device;
struct wrio_output;

WREI_OBJECT_EXPLICIT_DECLARE(wrio_context);

// -----------------------------------------------------------------------------

enum class wrio_event_type
{
    shutdown_requested,

    input_added,
    input_removed,
    input_event,

    output_added,       // Sent when an output is first detected
    output_removed,     // Sent before a output is removed from the output list
    output_configure,   // Sent when an output's configuration changes
    output_redraw,      // Sent when an output's content should be redrawn
};

// -----------------------------------------------------------------------------

enum class wrio_shutdown_reason
{
    no_more_outputs,        // Sent when no more outputs will be opened by the backend
    terminate_receieved,    // Sent when SIGTERM is received
    interrupt_receieved,    // Sent when SIGINT  is received
};

struct wrio_shutdown_event
{
    wrio_shutdown_reason reason;
};

// -----------------------------------------------------------------------------

enum class wrio_input_device_capability : u32
{
    libinput_led = 1 << 0,
};

struct wrio_input_channel
{
    u32 type;   // evdev type
    u32 code;   // evdev code
    f32 value;  // normalized channel value
};

/**
 * A grouping of associated input channel events.
 * This event reuses evdev codes for simplicity, with some minor changes:
 * - Values are sent as normalized floating point quantities.
 * - `REL_(H)WHEEL` events are sent with fractional detent deltas (~15 degrees / detent), instead of `REL_(H)WHEEL_HI_RES`.
 * - No `SYN_*` events are sent, instead `wrio_input_event` contains *all* events since the last report, and
 *   wrio always re-synchronizes internally.
 *
 * The `quiet` flag denotes that actions should not be taken in response to this event. E.g. on input
 * enter/leave events.
 *
 * Event design for touch, tablet and gesture controls is still pending.
 */
struct wrio_input_event
{
    wrio_input_device* device;
    bool quiet;
    std::span<const wrio_input_channel> channels;
};

// -----------------------------------------------------------------------------

struct wrio_output_event
{
    wrio_output* output;
    union {
        wren_image* target;
    };
};

// -----------------------------------------------------------------------------

struct wrio_event
{
    wrio_context* ctx;
    wrio_event_type type;

    union {
        wrio_shutdown_event shutdown;
        wrio_input_event    input;
        wrio_output_event   output;
    };
};

using wrio_event_handler = void(wrio_event*);

// -----------------------------------------------------------------------------

auto wrio_create(wrei_event_loop*, wren_context*) -> ref<wrio_context>;
void wrio_set_event_handler(wrio_context*, std::move_only_function<wrio_event_handler>&&);
void wrio_run( wrio_context*);
void wrio_stop(wrio_context*);

auto wrio_list_input_devices(wrio_context*) -> std::span<wrio_input_device* const>;
auto wrio_input_device_get_capabilities(wrio_input_device*) -> flags<wrio_input_device_capability>;
void wrio_input_device_update_leds(wrio_input_device*, flags<libinput_led>);

auto wrio_list_outputs(wrio_context*) -> std::span<wrio_output* const>;
void wrio_add_output(  wrio_context*);
void wrio_close_output(wrio_output*);

auto wrio_output_get_size(     wrio_output*) -> vec2u32;
void wrio_output_request_frame(wrio_output*, flags<wren_image_usage>);
void wrio_output_present(      wrio_output*, wren_image*, wren_syncpoint);
