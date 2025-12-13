#pragma once

#include "wren/shaders/shared.h"

struct wroc_shader_rect
{
    wren_image_handle<vec4f32> image;
    rect2f32 image_rect;

    rect2f32 rect;
};

struct wroc_shader_rect_input
{
    wroc_shader_rect* rects;

    vec2f32 output_size;
};
