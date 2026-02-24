#pragma once

#include "gpu/shaders/shared.h"

#ifndef GPU_SHADER
struct ImDrawVert;
#endif

struct wroc_imgui_shader_in
{
    gpu_const_ptr<ImDrawVert> vertices;
    vec2f32 scale;
    vec2f32 offset;
    image4f32 texture;
};
