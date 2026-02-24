#pragma once

#ifdef __cplusplus

#include "gpu/gpu.hpp"

using image4f32 = gpu_image_handle<vec4f32>;

template<typename T>
using gpu_const_ptr = const T*;

#else
#include "shared.slang"
#endif
