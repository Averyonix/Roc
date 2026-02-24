#pragma once

#include "scene/scene.hpp"

struct imui_context;
CORE_OBJECT_EXPLICIT_DECLARE(imui_context);

auto imui_create(scene_context*) -> ref<imui_context>;

// -----------------------------------------------------------------------------

void imui_request_frame(imui_context*);
auto imui_get_texture(imui_context*, gpu_image*, gpu_sampler*, gpu_blend_mode) -> ImTextureID;
