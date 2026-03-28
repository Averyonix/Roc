#pragma once

#include "scene/scene.hpp"

struct WayServer;

auto way_create(ExecContext*, Gpu*, Scene*) -> Ref<WayServer>;
