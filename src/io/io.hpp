#pragma once

#include "core/object.hpp"
#include "core/event.hpp"

#include "gpu/gpu.hpp"

// -----------------------------------------------------------------------------

struct io_context;
struct io_input_device;
struct io_output;

CORE_OBJECT_EXPLICIT_DECLARE(io_context);

// -----------------------------------------------------------------------------

enum class io_event_type
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

enum class io_shutdown_reason
{
    no_more_outputs,        // Sent when no more outputs will be opened by the backend
    terminate_receieved,    // Sent when SIGTERM is received
    interrupt_receieved,    // Sent when SIGINT  is received
};

struct io_shutdown_event
{
    io_shutdown_reason reason;
};

// -----------------------------------------------------------------------------

enum class io_input_device_capability : u32
{
    libinput_led = 1 << 0,
};

struct io_input_channel
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
 * - No `SYN_*` events are sent, instead `io_input_event` contains *all* events since the last report, and
 *   io always re-synchronizes internally.
 *
 * The `quiet` flag denotes that actions should not be taken in response to this event. E.g. on input
 * enter/leave events.
 *
 * Event design for touch, tablet and gesture controls is still pending.
 */
struct io_input_event
{
    io_input_device* device;
    bool quiet;
    std::span<const io_input_channel> channels;
};

// -----------------------------------------------------------------------------

struct io_output_event
{
    io_output* output;
    union {
        gpu_image* target;
    };
};

// -----------------------------------------------------------------------------

struct io_event
{
    io_context* ctx;
    io_event_type type;

    union {
        io_shutdown_event shutdown;
        io_input_event    input;
        io_output_event   output;
    };
};

using io_event_handler = void(io_event*);

// -----------------------------------------------------------------------------

auto io_create(core_event_loop*, gpu_context*) -> ref<io_context>;
void io_set_event_handler(io_context*, std::move_only_function<io_event_handler>&&);
void io_run( io_context*);
void io_stop(io_context*);

auto io_list_input_devices(io_context*) -> std::span<io_input_device* const>;
auto io_input_device_get_capabilities(io_input_device*) -> flags<io_input_device_capability>;
void io_input_device_update_leds(io_input_device*, flags<libinput_led>);

auto io_list_outputs(io_context*) -> std::span<io_output* const>;
void io_add_output(  io_context*);
void io_close_output(io_output*);

auto io_output_get_size(     io_output*) -> vec2u32;
void io_output_request_frame(io_output*, flags<gpu_image_usage>);
void io_output_present(      io_output*, gpu_image*, gpu_syncpoint);
