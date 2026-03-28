#pragma once

#ifdef __cplusplus

#include "gpu/gpu.hpp"

using image4f32 = GpuImageHandle<vec4f32>;

template<typename T>
using GpuConstPtr = const T*;

#else
#include "shared.slang"
#endif
