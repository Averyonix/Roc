#pragma once

#include "object.hpp"
#include "flags.hpp"

// -----------------------------------------------------------------------------

inline
bool core_fd_are_same(int fd0, int fd1)
{
    struct stat st0 = {};
    if (unix_check(fstat(fd0, &st0)).err()) return false;

    struct stat st1 = {};
    if (unix_check(fstat(fd0, &st1)).err()) return false;

    return st0.st_ino == st1.st_ino;
}

inline
int core_fd_dup_unsafe(int fd)
{
    if (fd < 0) return {};
    return unix_check(fcntl(fd, F_DUPFD_CLOEXEC, 0)).value;
}

// -----------------------------------------------------------------------------

struct core_event_loop;
struct core_fd;

enum class core_fd_event_bit : u32
{
    readable = 1 << 0,
    writable = 1 << 1,
};
using core_fd_event_bits = flags<core_fd_event_bit>;

enum class core_fd_listen_flag : u32
{
    oneshot = 1 << 0,
};
using core_fd_listen_flags = flags<core_fd_listen_flag>;

void core_fd_remove_listener(core_fd* fd);

using core_fd_listener_fn = void(core_fd*, core_fd_event_bits events);

struct core_fd_listener : core_object
{
    weak<core_event_loop> loop = nullptr;
    core_fd_event_bits events;
    core_fd_listen_flags flags;

    virtual void handle(core_fd* fd, core_fd_event_bits events) = 0;
};

struct core_fd : core_object
{
    int fd = -1;
    bool owned = true;
    ref<core_fd_listener> listener = {};

    int get()
    {
        return fd;
    }

    ~core_fd()
    {
        if (listener) core_fd_remove_listener(this);
        if (owned) close(fd);
    }
};

// -----------------------------------------------------------------------------

inline
ref<core_fd> core_fd_adopt(int fd)
{
    if (fd < 0) return {};
    auto container = core_create<core_fd>();
    container->fd = fd;
    return {container};
}

inline
ref<core_fd> core_fd_reference(int fd)
{
    if (fd < 0) return {};
    auto container = core_create<core_fd>();
    container->fd = fd;
    container->owned = false;
    return {container};
}

inline
ref<core_fd> core_fd_dup(int fd)
{
    if (fd < 0) return {};
    int dup_fd = core_fd_dup_unsafe(fd);
    if (dup_fd < 0) return {};
    auto container = core_create<core_fd>();
    container->fd = dup_fd;
    return {container};
}

// -----------------------------------------------------------------------------

void core_fd_set_listener(core_fd*, core_event_loop*, core_fd_listener* listener);

template<typename Fn>
void core_fd_set_listener(
    core_fd* fd,
    core_event_loop* loop,
    core_fd_event_bits events,
    Fn&& callback,
    core_fd_listen_flags flags = {})
{
    struct core_fd_listener_lambda : core_fd_listener
    {
        Fn lambda;
        core_fd_listener_lambda(Fn&& lambda): lambda(std::move(lambda)) {}
        virtual void handle(core_fd* fd, core_fd_event_bits events) { lambda(fd, events); }
    };

    auto listener = core_create<core_fd_listener_lambda>(std::move(callback));
    listener->events = events;
    listener->flags = flags;
    core_fd_set_listener(fd, loop, listener.get());
}
