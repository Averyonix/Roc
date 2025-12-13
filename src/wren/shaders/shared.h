#pragma once

#ifdef __cplusplus

#include "wren/wren.hpp"

template<typename T>
struct wren_image_handle
{
    u32 image   : 20 = {};
    u32 sampler : 12 = {};

    wren_image_handle() = default;

    wren_image_handle(wren_image* image, wren_sampler* sampler)
        : image(image->id)
        , sampler(sampler->id)
    {}
};

using image4f32 = wren_image_handle<vec4f32>;

#else
#include "shared.slang"
#endif
