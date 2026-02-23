#pragma once

#include "imui.hpp"

struct imui_context : wrei_object
{
    wrui_context* wrui;

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
};

void imui_init(imui_context*);
void imui_frame(imui_context*);
void imui_request_frame(imui_context*);
void imui_handle_key(imui_context*, xkb_keysym_t, bool pressed, const char* utf8);
void imui_handle_mods(imui_context*, flags<wrui_modifier>);
void imui_handle_motion(imui_context*);
void imui_handle_button(imui_context*, wrui_scancode, bool pressed);
void imui_handle_wheel(imui_context*, vec2f32 delta);
