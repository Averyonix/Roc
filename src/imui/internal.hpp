#pragma once

#include "imui.hpp"

struct imui_context : core_object
{
    io_context* io;
    gpu_context* gpu;
    scene_context* scene;

    ref<gpu_sampler> sampler;

    ImGuiContext* context;
    rect2f32 region;
    u32 frames_requested;
    ref<scene_tree> draws;
    struct texture {
        ref<gpu_image> image;
        ref<gpu_sampler> sampler;
        gpu_blend_mode blend;
    };
    std::vector<texture> textures;
    ref<gpu_image> font_image;
};

void imui_init_imgui(imui_context*);
void imui_frame(imui_context*);
void imui_handle_key(imui_context*, xkb_keysym_t, bool pressed, const char* utf8);
void imui_handle_mods(imui_context*, flags<scene_modifier>);
void imui_handle_motion(imui_context*);
void imui_handle_button(imui_context*, scene_scancode, bool pressed);
void imui_handle_wheel(imui_context*, vec2f32 delta);
