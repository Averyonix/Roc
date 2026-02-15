#pragma once

#include "wrei/object.hpp"
#include "wrei/event.hpp"

#include "wren/wren.hpp"

struct wrio_context;
struct wrio_input_device;
struct wrio_output;

struct wrio_node;
struct wrio_transform;
struct wrio_layer_stack;

// -----------------------------------------------------------------------------

WREI_OBJECT_EXPLICIT_DECLARE(wrio_context);

// -----------------------------------------------------------------------------

enum class wrio_pointer_axis
{
    horizontal,
    vertical,
};

enum class wrio_event_type
{
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
    output_modified,        // Sent when an output's configuration changes
    output_redraw,          // Sent before an output's content is rendered and committed
};

// An evdev key code - `[KEY|BTN]_*`
using wrio_key = u32;

struct wrio_input_event_data
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

struct wrio_event
{
    wrio_event_type type;

    union {
        wrio_input_event_data input;
        wrio_output*          output;
    };
};

using wrio_event_handler = void(wrio_event*);

auto wrio_context_create(std::move_only_function<wrio_event_handler>) -> ref<wrio_context>;
void wrio_context_run(wrio_context*);

auto wrio_context_list_input_devices(wrio_context*) -> std::span<ref<wrio_input_device>>;
auto wrio_context_list_outputs(      wrio_context*) -> std::span<ref<wrio_output>>;

auto wrio_context_add_output(wrio_context*) -> wrio_output*;
void wrio_context_close_output(wrio_output*);

void wrio_context_set_scene_root(wrio_layer_stack*);

// -----------------------------------------------------------------------------

enum class wrio_node_type
{
    transform,
    layer_stack,
    texture,
    output,
};

struct wrio_node : wrei_object
{
    wrio_node_type    type;         // Node type
    wrio_layer_stack* layer_parent; // Parent in the layer hierarchy, controls z-order and visibility
    wrio_transform*   transform;    // Parent in the transform hierarhcy, controls xy positioning
};

void wrio_node_unparent_transform(wrio_node*);
void wrio_node_unparent_layer(    wrio_node*);

struct wrio_transform : wrio_node
{
    vec2f32 translation; // Translation in transform parent's coordinate space
    f32     scale;       // Scale of nested transform coordinate space

    std::vector<ref<wrio_node>> children;
};

auto wrio_transform_create(wrio_context*) -> ref<wrio_transform>;
void wrio_transform_update(wrio_transform*, vec2f32 translation, f32 scale);
void wrio_transform_add_child(wrio_transform*, wrio_node* child);

struct wrio_layer_stack : wrio_node
{
    std::vector<ref<wrio_node>> children;
};

auto wrio_layer_stack_create(wrio_context) -> ref<wrio_layer_stack>;
void wrio_layer_stack_place_before(wrio_layer_stack*, wrio_node* reference, wrio_node* to_place);
void wrio_layer_stack_place_after( wrio_layer_stack*, wrio_node* reference, wrio_node* to_place);

struct wrio_texture : wrio_node
{
    ref<wren_image> image;
    vec4u8          tint;
    rect2f32        source;
    vec2f32         extent;
};

auto wrio_texture_create(wrio_context*) -> ref<wrio_texture>;
void wrio_texture_set_image( wrio_texture*, wren_image*);
void wrio_texture_set_tint(  wrio_texture*, vec4u8   tint);
void wrio_texture_set_source(wrio_texture*, rect2f32 source);
void wrio_texture_set_extent(wrio_texture*, vec2f32  extent);
void wrio_texture_damage(    wrio_texture*, rect2f32 damage);

struct wrio_output_node : wrio_node
{
    ref<wrio_output> output;
};

auto wrio_output_node_create(wrio_output*) -> ref<wrio_output_node>;
