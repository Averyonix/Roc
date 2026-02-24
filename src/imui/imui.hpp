#pragma once

#include "scene/scene.hpp"

struct imui_context;
CORE_OBJECT_EXPLICIT_DECLARE(imui_context);

auto imui_create(gpu_context*, scene_context*) -> ref<imui_context>;

// -----------------------------------------------------------------------------

using imui_frame_fn = void();

void imui_request_frame(imui_context*);
void imui_add_frame_handler(imui_context*, std::move_only_function<imui_frame_fn>&&);
auto imui_get_texture(imui_context*, gpu_image*, gpu_sampler*, gpu_blend_mode) -> ImTextureID;
