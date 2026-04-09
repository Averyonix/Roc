#pragma once

#include "wm/wm.hpp"

struct WayServer;

auto way_create(ExecContext*, Gpu*, WindowManager*) -> Ref<WayServer>;
