#pragma once

#include "wrui.hpp"

#include "wrio/wrio.hpp"

// -----------------------------------------------------------------------------

struct wrui_context
{
    wren_context* wren;
    wrio_context* wrio;

    struct {
        ref<wren_pipeline> premult;
        ref<wren_pipeline> postmult;
        ref<wren_image>    white;
        ref<wren_sampler>  sampler;
        flags<wren_image_usage> usage;
    } render;

    ref<wrui_transform> root_transform;
    ref<wrui_tree> scene;

    std::vector<wrui_client*> clients;
    std::vector<wrui_window*> windows;

    ref<wrui_keyboard> keyboard;
    ref<wrui_pointer>  pointer;

    struct {
        ImGuiContext* context;
        rect2f32 region;
        u32 frames_requested;
        ref<wrui_tree> draws;
        struct texture {
            ref<wren_image> image;
            ref<wren_sampler> sampler;
            wren_blend_mode blend;
        };
        std::vector<texture> textures;
        ref<wren_image> font_image;
    } imgui;
};

void wrui_render_init(wrui_context*);
void wrui_render(wrui_context*, wrio_output*, wren_image*);

void wrui_imgui_init(wrui_context*);
void wrui_imgui_frame(wrui_context*);
void wrui_imgui_request_frame(wrui_context*);
void wrui_imgui_handle_key(wrui_context*, xkb_keysym_t, bool pressed, const char* utf8);
void wrui_imgui_handle_mods(wrui_context*, flags<wrui_modifier>);
void wrui_imgui_handle_motion(wrui_context*);
void wrui_imgui_handle_button(wrui_context*, wrui_scancode, bool pressed);
void wrui_imgui_handle_wheel(wrui_context*, vec2f32 delta);

struct wrui_client
{
    wrui_context* ctx;

    std::move_only_function<wrui_event_handler_fn> event_handler;

    ~wrui_client();
};

void wrui_client_post_event(wrui_client*, wrui_event*);

struct wrui_window
{
    wrui_client* client;

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
    ref<wrui_texture>   visual;
};

auto wrui_pointer_create(wrui_context*) -> ref<wrui_pointer>;

void wrui_handle_input_added(wrui_context*, wrio_input_device*);
void wrui_handle_input_removed(wrui_context*, wrio_input_device*);
void wrui_handle_input(wrui_context*, const wrio_input_event&);
