#pragma once

#include "scene/scene.hpp"

// This is a temporary hack while both versions of wroc exist in the codebase
// simultaneously to prevent clangd from applying refactors across both versions.
#define WROC_NAMESPACE_USE using namespace wrocx;
#define WROC_NAMESPACE_BEGIN namespace wrocx {
#define WROC_NAMESPACE_END }

WROC_NAMESPACE_BEGIN

struct wroc_server;

auto wroc_create(wrei_event_loop*, wren_context*, wrui_context*) -> ref<wroc_server>;

WROC_NAMESPACE_END
WROC_NAMESPACE_USE

WREI_OBJECT_EXPLICIT_DECLARE(wroc_server);
