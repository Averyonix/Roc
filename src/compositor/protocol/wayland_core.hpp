#pragma once

#include "common/types.hpp"
#include "common/util.hpp"
#include "common/event_loop.hpp"

namespace wayland::server
{

using fx32 = u32;

struct Client;
struct ClientId
{
    Client* client;
    u32 id;
};
struct Object
{
    u32 interface_id = 0;
    std::vector<ClientId> client_ids;
};

struct Display;

Display* display_create(std::string_view socket_name, EventLoop*);

} // namespace wayland::server
