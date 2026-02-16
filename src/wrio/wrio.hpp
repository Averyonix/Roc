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
    input_leave,            // Sent when the state of input becomes unreadable.
    input_key_enter,        // Sent when a key is discovered to already be pressed (does not trigger on-press actions)
    input_key_press,        // Sent when a key or button is pressed
    input_key_release,      // Sent when a key or button is released
    input_pointer_motion,   // Sent when a pointer moves
    input_pointer_axis,     // Sent when a pointer axis is moved

    output_added,           // Sent when an output is first detected
    output_removed,         // Sent before a output is removed from the output list
    output_configure,       // Sent when an output's configuration changes
    output_redraw,          // Sent when an output's content should be redrawn
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

using wrio_key = u32;       // An evdev key code - `[KEY|BTN]_*`

enum class wrio_pointer_axis
{
    horizontal,
    vertical,
};

struct wrio_input_event
{
    wrio_input_device* device;
    union {
        wrio_key key;
        vec2f64  motion;
        struct {
            wrio_pointer_axis axis;
            f64               delta;
        } axis;
    };
};

// -----------------------------------------------------------------------------

struct wrio_output_event
{
    wrio_output* output;
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

auto wrio_context_create() -> ref<wrio_context>;
void wrio_context_set_event_handler(wrio_context*, std::move_only_function<wrio_event_handler>&&);
void wrio_context_run(wrio_context*);
void wrio_context_stop(wrio_context*);

auto wrio_context_list_input_devices(wrio_context*) -> std::span<wrio_input_device*>;
auto wrio_context_list_outputs(      wrio_context*) -> std::span<wrio_output*>;

auto wrio_context_add_output(wrio_context*) -> wrio_output*;
void wrio_context_close_output(wrio_output*);

void wrio_output_request_frame(wrio_output*);
