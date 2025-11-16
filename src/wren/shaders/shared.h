#pragma once

#ifdef __cplusplus

#include "wrei/types.hpp"

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
