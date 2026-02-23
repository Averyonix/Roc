#pragma once

#include "wrui/wrui.hpp"

struct imui_context;
WREI_OBJECT_EXPLICIT_DECLARE(imui_context);

auto imui_create(wrui_context*) -> ref<imui_context>;

// -----------------------------------------------------------------------------

void imui_request_frame(imui_context*);
auto imui_get_texture(imui_context*, wren_image*, wren_sampler*, wren_blend_mode) -> ImTextureID;
