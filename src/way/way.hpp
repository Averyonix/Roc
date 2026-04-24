#pragma once

#include <wm/wm.hpp>

struct WayServer;

auto way_create(ExecContext*, Gpu*, WindowManager*) -> Ref<WayServer>;

auto way_server_get_socket(WayServer*) -> const char*;
