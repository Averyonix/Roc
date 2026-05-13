#pragma once

#include <wm/wm.hpp>

struct WayServer;

auto way_create(WmServer*, ExecContext*) -> Ref<WayServer>;

auto way_server_get_socket(WayServer*) -> const char*;

void way_clear(WayServer*);
