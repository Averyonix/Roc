#pragma once

#include "wayland_core.hpp"

namespace wayland::server
{

struct Client
{
    Display* display;

    int fd;
    u32 next_id = 1;

    std::unordered_map<u32, Object*> objects;
};

struct Display
{
    int fd;
    EventLoop* event_loop;

    std::vector<Client*> clients;

    struct wl_display* wl_display;
};

struct MessageHeader
{
    u32 object_id;
    u16 opcode;
    u16 size;
};

struct Message
{
    MessageHeader header;
    u8* data;
};

template<typename T>
T align_up_power2(T v, u64 align) noexcept
{
    return T((u64(v) + (align - 1)) &~ (align - 1));
}

inline
u32 message_read_uint(Message& message)
{
    u32 v;
    std::memcpy(&v, message.data, sizeof(v));
    message.data += 4;
    return v;
}

inline
i32 message_read_int(Message& message)
{
    i32 v;
    std::memcpy(&v, message.data, sizeof(v));
    message.data += 4;
    return v;
}

inline
std::string_view message_read_string(Message& message)
{
    u32 len = message_read_uint(message);
    std::string_view v{reinterpret_cast<const char*>(message.data), len};
    message.data += align_up_power2(len, 4);
    return v;
}

inline
std::span<const u8> message_read_array(Message& message)
{
    u32 len = message_read_uint(message);
    std::span<const u8> v{message.data, len};
    message.data += align_up_power2(len, 4);
    return v;
}

inline
double message_read_fixed(Message& message)
{
    u32 raw = message_read_uint(message);
    return raw / 256.0;
}

inline
void register_object(Object* obj, Client* client, int id)
{
    client->objects.emplace(id, obj);
    obj->client_ids.emplace_back(client, id);
}

template<typename T>
T* message_read_new_id(Message& message, Client* client)
{
    u32 id = message_read_uint(message);
    T* obj = new T {};
    register_object(obj, client, id);
    return obj;
}

template<typename T>
T* message_read_object(Message& message, Client* client, u32 interface_id)
{
    u32 id = message_read_uint(message);
    auto obj_iter = client->objects.find(id);
    if (obj_iter == client->objects.end()) {
        log_error("Invalid object ID {}", id);
        return nullptr;
    }
    if (obj_iter->second->interface_id != interface_id) {
        log_error("Expected interface {}, got {}", interface_id, obj_iter->second->interface_id);
    }
    return static_cast<T*>(obj_iter->second);
}

template<typename E>
E message_read_enum(Message& message)
{
    return E(message_read_uint(message));
}

using DispatchFn = void(*)(Client* client, Object* object, Message);
extern const std::span<const std::span<const DispatchFn>> dispatch_table_view;

} // namespace wayland::server
