#pragma once

#include "wren/shaders/shared.h"

#ifndef WREN_SHADER
struct ImDrawVert;
#endif

struct wroc_imgui_shader_in
{
    wren_const_ptr<ImDrawVert> vertices;
    vec2f32 scale;
    vec2f32 offset;
    image4f32 texture;
};
