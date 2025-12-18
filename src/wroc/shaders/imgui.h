#pragma once

#include "wren/shaders/shared.h"

struct wroc_imgui_shader_in
{
    ImDrawVert* vertices;
    vec2f32 scale;
    vec2f32 offset;
    image4f32 texture;
};
