#pragma once

#include "gpu/shaders/shared.h"

struct wrui_vertex
{
    vec2f32 pos;
    vec2f32 uv;
    vec4u8 color;
};

struct wrui_render_input
{
    wren_const_ptr<wrui_vertex> vertices;
    vec2f32 scale;
    vec2f32 offset;
    image4f32 texture;
};
