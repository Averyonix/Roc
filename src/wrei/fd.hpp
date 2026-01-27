#include "object.hpp"

struct wrei_fd_t : wrei_object
{
    int fd = -1;

    ~wrei_fd_t()
    {
        close(fd);
    }
};

struct wrei_fd
{
    ref<wrei_fd_t> holder;

    int get() const noexcept { return holder ? holder->fd : -1; }

    explicit operator bool() const noexcept { return get() >= 0; }
};

inline
bool wrei_fd_are_same(const wrei_fd& fd0, const wrei_fd& fd1)
{
    struct stat st0 = {};
    if (wrei_unix_check_n1(fstat(fd0.get(), &st0))) return false;

    struct stat st1 = {};
    if (wrei_unix_check_n1(fstat(fd0.get(), &st1))) return false;

    return st0.st_ino == st1.st_ino;
}

inline
wrei_fd wrei_fd_adopt(int fd)
{
    if (fd < 0) return {};
    auto container = wrei_create<wrei_fd_t>();
    container->fd = fd;
    return {container};
}

inline
int wrei_fd_dup_unsafe(int fd)
{
    if (fd < 0) return {};
    return wrei_unix_check_n1(fcntl(fd, F_DUPFD_CLOEXEC, 0));
}

inline
wrei_fd wrei_fd_dup(int fd)
{
    if (fd < 0) return {};
    int dup_fd = wrei_fd_dup_unsafe(fd);
    if (dup_fd < 0) return {};
    auto container = wrei_create<wrei_fd_t>();
    container->fd = dup_fd;
    return {container};
}
