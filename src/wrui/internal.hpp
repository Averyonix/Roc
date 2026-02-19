#pragma once

#include "wrui.hpp"

#include "wrio/wrio.hpp"

// -----------------------------------------------------------------------------

struct wrui_context
{
    wren_context* wren;
    wrio_context* wrio;

    ref<wren_pipeline> pipeline;
    ref<wren_image> white;
    ref<wren_sampler> sampler;

    ref<wrui_transform> root_transform;
    ref<wrui_tree> scene;

    std::vector<wrui_window*> windows;

    ref<wrui_keyboard> keyboard;
    ref<wrui_pointer>  pointer;
};

void wrui_render_init(wrui_context*);
void wrui_render(wrui_context*, wrio_output*, wren_image*);

struct wrui_window
{
    wrui_context* ctx;

    std::move_only_function<wrui_event_handle_fn> event_handler;

    vec2u32 size;
    bool mapped;

    ref<wrui_tree> tree;
    ref<wrui_transform> transform;

    ~wrui_window();
};

struct wrui_keyboard
{
    wrei_counting_set<u32> pressed;

    std::vector<wrio_input_device*> led_devices;

    struct xkb_context* context;
    struct xkb_state*   state;
    struct xkb_keymap*  keymap;

    wrei_enum_map<wrui_modifier, xkb_mod_mask_t> mod_masks;

    i32 rate = 25;
    i32 delay = 600;

    ~wrui_keyboard();
};

auto wrui_keyboard_create(wrui_context*) -> ref<wrui_keyboard>;

struct wrui_pointer
{
    wrei_counting_set<u32> pressed;

    ref<wrui_transform> transform;
    ref<wrui_texture>   texture;
};

auto wrui_pointer_create(wrui_context*) -> ref<wrui_pointer>;

void wrui_handle_input_added(wrui_context*, wrio_input_device*);
void wrui_handle_input_removed(wrui_context*, wrio_input_device*);
void wrui_handle_input(wrui_context*, const wrio_input_event&);
