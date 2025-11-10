#pragma once

#include "common/types.hpp"
#include "common/util.hpp"
#include "common/event_loop.hpp"

namespace wayland::server
{

struct NewId
{
    std::string_view interface;
    u32 version;
    u32 new_id;
};

struct Client;

struct ClientId
{
    Client* client;
    u32 id;
};

struct Object
{
    u32 _interface_id;
    u32 _name;
    std::vector<ClientId> _client_ids;
};

struct Display;

Display* display_from_client(Client*);
Display* display_create(std::string_view socket_name, EventLoop*);
u32 display_allocate_id(Display* display);

} // namespace wayland::server
