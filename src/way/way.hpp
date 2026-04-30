#pragma once

#include <wm/wm.hpp>

struct WayServer;

auto way_create(ExecContext*, Gpu*, WmServer*) -> Ref<WayServer>;

auto way_server_get_socket(WayServer*) -> const char*;

void way_clear(WayServer*);
