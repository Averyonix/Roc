#pragma once

#include "wren/shaders/shared.h"

struct wren_shader_surface_in
{
    wren_image_handle<vec4f32> image;

    vec2f32 src_origin;
    vec2f32 src_extent;

    vec2f32 dst_origin;
    vec2f32 dst_extent;
};
