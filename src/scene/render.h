#pragma once

#include "gpu/shaders/shared.h"

struct scene_vertex
{
    vec2f32 pos;
    vec2f32 uv;
    vec4u8 color;
};

struct scene_render_input
{
    gpu_const_ptr<scene_vertex> vertices;
    vec2f32 scale;
    vec2f32 offset;
    image4f32 texture;
};
