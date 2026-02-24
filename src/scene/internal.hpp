#pragma once

#include "scene.hpp"

#include "io/io.hpp"

// -----------------------------------------------------------------------------

struct scene_output {
    scene_context* ctx;
    io_output*     io;
    rect2f32       viewport;
};

// -----------------------------------------------------------------------------

struct scene_context
{
    gpu_context* gpu;
    io_context* io;

    struct {
        ref<gpu_pipeline> premult;
        ref<gpu_pipeline> postmult;
        ref<gpu_image>    white;
        ref<gpu_sampler>  sampler;
        flags<gpu_image_usage> usage;
    } render;

    ref<scene_transform> root_transform;
    ref<scene_tree>      root_tree;
    core_enum_map<scene_layer, ref<scene_tree>> layers;

    core_ref_vector<scene_output> outputs;

    std::vector<scene_client*> clients;
    std::vector<scene_window*> windows;

    ref<scene_keyboard> keyboard;
    ref<scene_pointer>  pointer;
};

void scene_broadcast_event(scene_context*, scene_event*);

void scene_render_init(scene_context*);
void scene_render(scene_context*, scene_output*, gpu_image*);

// -----------------------------------------------------------------------------

struct scene_client
{
    scene_context* ctx;

    std::move_only_function<scene_event_handler_fn> event_handler;

    ~scene_client();
};

void scene_client_post_event(scene_client*, scene_event*);

// -----------------------------------------------------------------------------

struct scene_window
{
    scene_client* client;

    vec2u32 size;
    bool mapped;

    std::string title;

    ref<scene_tree> tree;
    ref<scene_transform> transform;

    ~scene_window();
};

// -----------------------------------------------------------------------------

struct scene_keyboard
{
    core_counting_set<u32> pressed;

    std::vector<io_input_device*> led_devices;

    struct xkb_context* context;
    struct xkb_state*   state;
    struct xkb_keymap*  keymap;

    core_enum_map<scene_modifier, xkb_mod_mask_t> mod_masks;

    i32 rate = 25;
    i32 delay = 600;

    scene_focus focus;

    ~scene_keyboard();
};

auto scene_keyboard_create(scene_context*) -> ref<scene_keyboard>;

// -----------------------------------------------------------------------------

struct scene_pointer
{
    core_counting_set<u32> pressed;

    ref<scene_transform> transform;
    ref<scene_texture>   visual;

    std::move_only_function<scene_pointer_driver_fn> driver;

    scene_client* grab;
    scene_focus   focus;
};

void scene_update_pointer_focus(scene_context*);

auto scene_pointer_create(scene_context*) -> ref<scene_pointer>;

// -----------------------------------------------------------------------------

void scene_handle_input_added(scene_context*, io_input_device*);
void scene_handle_input_removed(scene_context*, io_input_device*);
void scene_handle_input(scene_context*, const io_input_event&);
