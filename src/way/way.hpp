#pragma once

#include "scene/scene.hpp"

struct way_server;
CORE_OBJECT_EXPLICIT_DECLARE(way_server);

auto way_create(core_event_loop*, gpu_context*, scene_context*) -> ref<way_server>;
