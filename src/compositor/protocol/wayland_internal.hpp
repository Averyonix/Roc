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

    u32 next_id = 1;
};

inline
void register_object(Object* obj, Client* client, int id)
{
    client->objects.emplace(id, obj);
    obj->_client_ids.emplace_back(client, id);
}

inline
u32 object_get_id(Object* obj, Client* client)
{
    for (auto[c, id] : obj->_client_ids) {
        if (c == client) return id;
    }
    return 0;
}

// -----------------------------------------------------------------------------

struct MessageHeader
{
    u32 object_id;
    u16 opcode;
    u16 size;
};

struct Message
{
    MessageHeader header;
    u8 data[65536 - 8];
};

template<typename T>
T align_up_power2(T v, u64 align) noexcept
{
    return T((u64(v) + (align - 1)) &~ (align - 1));
}

// -----------------------------------------------------------------------------

struct MessageReader
{
    Message* message;
    u32 offset;

    u32 read_uint()
    {
        u32 v;
        std::memcpy(&v, message->data + offset, sizeof(v));
        offset += 4;
        return v;
    }

    i32 read_int()
    {
        i32 v;
        std::memcpy(&v, message->data + offset, sizeof(v));
        offset += 4;
        return v;
    }

    std::string_view read_string()
    {
        u32 len_with_nul = read_uint();
        std::string_view v{reinterpret_cast<const char*>(message->data + offset), len_with_nul - 1};
        offset += align_up_power2(len_with_nul, 4);
        return v;
    }

    std::span<const u8> read_array()
    {
        u32 len = read_uint();
        std::span<const u8> v{message->data + offset, len};
        offset += align_up_power2(len, 4);
        return v;
    }

    double read_fixed()
    {
        return read_uint() / 256.0;
    }

    template<typename T>
    T* read_new_id(Client* client)
    {
        u32 id = read_uint();
        T* obj = new T {};
        register_object(obj, client, id);
        return obj;
    }

    NewId read_untyped_new_id(Client* client)
    {
        auto interface_name = read_string();
        u32 version = read_uint();
        u32 new_id = read_uint();
        return NewId {
            .interface = interface_name,
            .version = version,
            .new_id = new_id,
        };
    }

    template<typename T>
    T* read_object(Client* client, u32 interface_id)
    {
        u32 id = read_uint();
        auto obj_iter = client->objects.find(id);
        if (obj_iter == client->objects.end()) {
            log_error("Invalid object ID {}", id);
            return nullptr;
        }
        if (obj_iter->second->_interface_id != interface_id) {
            log_error("Expected interface {}, got {}", interface_id, obj_iter->second->_interface_id);
            return nullptr;
        }
        return static_cast<T*>(obj_iter->second);
    }

    template<typename E>
    E read_enum()
    {
        return E(read_uint());
    }
};

using DispatchFn = void(*)(Client* client, Object* object, MessageReader);
extern const std::span<const std::span<const DispatchFn>> dispatch_table_view;

// -----------------------------------------------------------------------------

struct MessageWriter
{
    Message* message;
    u32 offset;

    void write_uint(u32 v)
    {
        std::memcpy(message->data + offset, &v, sizeof(v));
        offset += sizeof(v);
    }

    void write_int(i32 v)
    {
        std::memcpy(message->data + offset, &v, sizeof(v));
        offset += sizeof(v);
    }

    void write_fixed(double v)
    {
        write_uint(v * 256.0);
    }

    void write_string(std::string_view str)
    {
        auto len = str.size();
        write_uint(len + 1);
        std::memcpy(message->data + offset, str.data(), len);
        message->data[offset + len] = '\0';
        offset += align_up_power2(len + 1, 4);
    }

    void write_object(Object* object, Client* client)
    {
        write_uint(object_get_id(object, client));
    }

    void write_array(std::span<const u8> array)
    {
        write_uint(array.size());
        std::memcpy(message->data + offset, array.data(), array.size());
        offset += align_up_power2(array.size(), 4);
    }

    template<typename E>
    void write_enum(E e)
    {
        write_uint(std::to_underlying(e));
    }

    void write_header(u32 object_id, u16 opcode)
    {
        message->header.object_id = object_id;
        message->header.opcode = opcode;
        message->header.size = offset + 8;
    }
};

void display_send_event(Client* client, const Message& message);

} // namespace wayland::server
