#pragma once

#include "gpu/shaders/shared.h"

struct SceneVertex
{
    vec2f32 pos;
    vec2f32 uv;
    vec4u8 color;
};

struct SceneRenderInput
{
    GpuConstPtr<SceneVertex> vertices;
    vec2f32 scale;
    vec2f32 offset;
    image4f32 texture;
};
