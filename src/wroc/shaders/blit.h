#pragma once

#include "wren/shaders/shared.h"

struct wroc_shader_rect
{
    image4f32 image;
    rect2f32  image_rect;
    int       image_has_alpha;

    rect2f32 rect;

    float opacity;
    vec4f32 color;
};

struct wroc_shader_rect_input
{
    wroc_shader_rect* rects;

    vec2f32 output_size;
};
