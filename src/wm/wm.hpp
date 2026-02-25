#pragma once

#include "scene/scene.hpp"

enum class wm_movesize_mode
{
    move,
    size,
};

struct wm_context
{
    scene_context* scene;

    scene_modifier main_mod;

    struct {
        ref<scene_client> client;
        weak<scene_window> window;

        vec2f32  grab;
        rect2f32 frame;
        vec2f32  relative;

        wm_movesize_mode mode;
    } movesize;
};

auto wm_create(scene_context*) -> ref<wm_context>;

void wm_init_movesize(wm_context*);
