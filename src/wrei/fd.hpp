#pragma once

#include "object.hpp"
#include "flags.hpp"

// -----------------------------------------------------------------------------

inline
bool wrei_fd_are_same(int fd0, int fd1)
{
    struct stat st0 = {};
    if (unix_check(fstat(fd0, &st0)).err()) return false;

    struct stat st1 = {};
    if (unix_check(fstat(fd0, &st1)).err()) return false;

    return st0.st_ino == st1.st_ino;
}

inline
int wrei_fd_dup_unsafe(int fd)
{
    if (fd < 0) return {};
    return unix_check(fcntl(fd, F_DUPFD_CLOEXEC, 0)).value;
}

// -----------------------------------------------------------------------------

struct wrei_event_loop;
struct wrei_fd;

enum class wrei_fd_event_bit : u32
{
    readable = 1 << 0,
    writable = 1 << 1,
};
using wrei_fd_event_bits = flags<wrei_fd_event_bit>;

enum class wrei_fd_listen_flag : u32
{
    oneshot = 1 << 0,
};
using wrei_fd_listen_flags = flags<wrei_fd_listen_flag>;

void wrei_fd_remove_listener(wrei_fd* fd);

using wrei_fd_listener_fn = void(wrei_fd*, wrei_fd_event_bits events);

struct wrei_fd_listener : wrei_object
{
    weak<wrei_event_loop> loop = nullptr;
    wrei_fd_event_bits events;
    wrei_fd_listen_flags flags;

    virtual void handle(wrei_fd* fd, wrei_fd_event_bits events) = 0;
};

struct wrei_fd : wrei_object
{
    int fd = -1;
    bool owned = true;
    ref<wrei_fd_listener> listener = {};

    int get()
    {
        return fd;
    }

    ~wrei_fd()
    {
        if (listener) wrei_fd_remove_listener(this);
        if (owned) close(fd);
    }
};

// -----------------------------------------------------------------------------

inline
ref<wrei_fd> wrei_fd_adopt(int fd)
{
    if (fd < 0) return {};
    auto container = wrei_create<wrei_fd>();
    container->fd = fd;
    return {container};
}

inline
ref<wrei_fd> wrei_fd_reference(int fd)
{
    if (fd < 0) return {};
    auto container = wrei_create<wrei_fd>();
    container->fd = fd;
    container->owned = false;
    return {container};
}

inline
ref<wrei_fd> wrei_fd_dup(int fd)
{
    if (fd < 0) return {};
    int dup_fd = wrei_fd_dup_unsafe(fd);
    if (dup_fd < 0) return {};
    auto container = wrei_create<wrei_fd>();
    container->fd = dup_fd;
    return {container};
}

// -----------------------------------------------------------------------------

void wrei_fd_set_listener(wrei_fd*, wrei_event_loop*, wrei_fd_listener* listener);

template<typename Fn>
void wrei_fd_set_listener(
    wrei_fd* fd,
    wrei_event_loop* loop,
    wrei_fd_event_bits events,
    Fn&& callback,
    wrei_fd_listen_flags flags = {})
{
    struct wrei_fd_listener_lambda : wrei_fd_listener
    {
        Fn lambda;
        wrei_fd_listener_lambda(Fn&& lambda): lambda(std::move(lambda)) {}
        virtual void handle(wrei_fd* fd, wrei_fd_event_bits events) { lambda(fd, events); }
    };

    auto listener = wrei_create<wrei_fd_listener_lambda>(std::move(callback));
    listener->events = events;
    listener->flags = flags;
    wrei_fd_set_listener(fd, loop, listener.get());
}
