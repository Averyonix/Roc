#include "wayland_core.hpp"
#include "wayland_internal.hpp"
#include "compositor/protocol/wayland_server.hpp"

namespace wayland::server
{

Display* display_from_client(Client* client)
{
    return client->display;
}

u32 display_allocate_id(Display* display)
{
    return display->next_id++;
}

static
sockaddr_un socket_addr_from_name(std::string_view path)
{
    sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    size_t len = std::min(path.size(), sizeof(addr.sun_path) - 1);
    std::memcpy(addr.sun_path, path.data(), len);
    addr.sun_path[len] = '\0';
    return addr;
}

static
void display_add_socket(Display* display, std::string_view socket_name)
{
    display->fd = unix_check_n1(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));

    auto socket_path = std::filesystem::path(getenv("XDG_RUNTIME_DIR")) / socket_name;
    auto socket_addr = socket_addr_from_name(socket_path.c_str());
    unix_check_n1(unlink(socket_addr.sun_path));
    unix_check_n1(bind(display->fd, reinterpret_cast<sockaddr*>(&socket_addr), sizeof(socket_addr)));
    unix_check_n1(listen(display->fd, 8));
}

void display_disconnect_client(Client* client)
{
    event_loop_remove_fd(client->display->event_loop, client->fd);
}

void display_read(void* data, int fd, u32 events)
{
    log_trace("display_read(fd = {}, events = {:#x})", fd, events);

    auto* client = static_cast<Client*>(data);

    if (events & EPOLLHUP) {
        log_warn("Client hung up");
        display_disconnect_client(client);
        return;
    }

    Message message;
    auto& header = message.header;
    size_t len = unix_check_n1(recv(fd, &header, sizeof(header), MSG_NOSIGNAL));
    if (len != sizeof(header)) {
        log_error("Expected {} header bytes, got {}", sizeof(header), len);
        display_disconnect_client(client);
        return;
    }

    log_trace("MessageHeader(obj = {}, op = {}, size = {})", header.object_id, header.opcode, header.size);
    log_trace("  ({}, {})", header.object_id >> 16, header.object_id & 0xFFFF);

    if (header.size < sizeof(header)) {
        log_error("Header size {} too small", header.size);
        display_disconnect_client(client);
        return;
    }

    auto obj_iter = client->objects.find(header.object_id);
    if (obj_iter == client->objects.end()) {
        log_error("Invalid object {} does not map to any known objects", header.object_id);
        display_disconnect_client(client);
        return;
    }

    u32 remaining_len = header.size - sizeof(MessageHeader);

    if (remaining_len) {
        len = unix_check_n1(recv(fd, message.data, remaining_len, 0));
        if (len != remaining_len) {
            log_error("Expected {} message bytes, got {}", remaining_len, len);
            display_disconnect_client(client);
            return;
        }
    }

    {
        Object* object = obj_iter->second;

        log_debug("display_dispatch_message, interface_id = {}, opcode = {}", object->_interface_id, message.header.opcode);
        if (object->_interface_id > dispatch_table_view.size()) {
            log_error("Interface ID {} out of range (0..={})", object->_interface_id, dispatch_table_view.size() - 1);
            display_disconnect_client(client);
            return;
        }

        auto dispatch_table = dispatch_table_view[object->_interface_id];

        if (dispatch_table.empty()) {
            log_error("Interface has no dispatch table");
            display_disconnect_client(client);
            return;
        }

        if (message.header.opcode > dispatch_table.size()) {
            log_error("Opcode {} out of range (0..={})", message.header.opcode, dispatch_table.size() - 1);
            display_disconnect_client(client);
            return;
        }

        dispatch_table[message.header.opcode](client, object, MessageReader{&message});
    }
}

void display_send_event(Client* client, const Message& message)
{
    unix_check_n1(send(client->fd, &message, message.header.size, MSG_NOSIGNAL));
}

void display_accept(void* data, int fd, u32 events)
{
    log_warn("display_accept(fd = {}, events = {:#x})", fd, events);

    auto* display = static_cast<Display*>(data);

    auto client_fd = unix_check_n1(accept(fd, nullptr, nullptr));

    if (client_fd < 0) return;

    auto* client = new Client {};
    client->fd = client_fd;
    client->display = display;
    display->clients.emplace_back(client);

    register_object(display->wl_display, client, 1);

    event_loop_add_fd(display->event_loop, client_fd, EPOLLIN, display_read, client);
}

Display* display_create(std::string_view socket_name, EventLoop* event_loop)
{
    auto* display = new Display {};
    display->event_loop = event_loop;

    display_add_socket(display, socket_name);

    event_loop_add_fd(event_loop, display->fd, EPOLLIN, display_accept, display);

    display->wl_display = new wl_display(display);

    log_debug("display object created, name = {}", display->wl_display->_name);

    return display;
}

} // namespace wayland::server
