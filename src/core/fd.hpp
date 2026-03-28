#pragma once

#include "types.hpp"

// -----------------------------------------------------------------------------

static constexpr int fd_limit = 1024;

// -----------------------------------------------------------------------------

auto fd_are_same(int fd0, int fd1) -> bool;
auto fd_dup_unsafe(int fd) -> int;

// -----------------------------------------------------------------------------

inline
bool fd_is_valid(int fd)
{
    return fd >= 0 && fd < fd_limit;
}

auto fd_get_ref_count(int fd) -> u32;

auto fd_add_ref(   int fd) -> int;
auto fd_remove_ref(int fd) -> int;

auto fd_extract(int fd) -> int;

struct Fd
{
    int fd;

    Fd()
        : fd(-1)
    {}

    explicit Fd(int fd)
        : fd(fd)
    {
        fd_add_ref(fd);
    }

    Fd(const Fd& other)
        : fd(other.fd)
    {
        fd_add_ref(fd);
    }

    Fd& operator=(const Fd& other)
    {
        if (this != &other) {
            reset(other.fd);
        }
        return *this;
    }

    Fd(Fd&& other)
        : fd(std::exchange(other.fd, -1))
    {}

    Fd& operator=(Fd&& other)
    {
        if (this != &other) {
            fd_remove_ref(fd);
            fd = std::exchange(other.fd, -1);
        }
        return *this;
    }

    ~Fd()
    {
        fd_remove_ref(fd);
    }

    void reset(int new_fd = -1)
    {
        fd_remove_ref(fd);
        fd = fd_add_ref(new_fd);
    }

    Fd& operator=(std::nullptr_t)
    {
        reset();
        return *this;
    }

    int get() const noexcept { return fd; }

    int extract() noexcept;

    explicit operator bool() const noexcept { return fd >= 0; }
};
