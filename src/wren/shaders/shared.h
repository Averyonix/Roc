#pragma once

#ifdef __cplusplus

#include "wrei/types.hpp"

using vec4f32 = wrei_vec4f32;
using vec3f32 = wrei_vec3f32;
using vec2f32 = wrei_vec2f32;

struct wren_image_handle_base
{
    u32 image   : 20 = {};
    u32 sampler : 12 = {};
};

template<typename T>
struct wren_image_handle : wren_image_handle_base {};

#else
#include "shared.slang"
#endif
