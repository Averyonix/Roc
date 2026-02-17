#pragma once

#ifdef __cplusplus

#include "wren/wren.hpp"

using image4f32 = wren_image_handle<vec4f32>;

template<typename T>
using wren_const_ptr = const T*;

#else
#include "shared.slang"
#endif
