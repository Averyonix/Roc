#include "fd.hpp"

#include "debug.hpp"
#include "log.hpp"

auto fd_are_same(fd_t fd0, fd_t fd1) -> bool
{
    struct stat st0 = {};
    if (unix_check<fstat>(fd0, &st0).err()) return false;

    struct stat st1 = {};
    if (unix_check<fstat>(fd0, &st1).err()) return false;

    return st0.st_ino == st1.st_ino;
}

auto fd_dup_unsafe(fd_t fd) -> fd_t
{
    if (fd < 0) return {};

    return unix_check<fcntl>(fd, F_DUPFD_CLOEXEC, 0).value;
}

// -----------------------------------------------------------------------------

#define FD_LEAK_CHECK 1

struct FdRegistry
{
    std::array<u32,  fd_limit> ref_counts = {};
#if FD_LEAK_CHECK
    std::array<bool, fd_limit> inherited  = {};
#endif

#if FD_LEAK_CHECK
    FdRegistry()
    {
        for (fd_t fd = 0; fd < fd_limit; ++fd) {
            if (fcntl(fd, F_GETFD) == 0) {
                inherited[fd] = true;
            }
        }
    }

    ~FdRegistry()
    {
        for (fd_t fd = 0; fd < fd_limit; ++fd) {
            if (inherited[fd]) continue;
            if (fcntl(fd, F_GETFD) == -1) continue;

            log_error("fd[{}] leaked (refs: {})", fd, ref_counts[fd]);
        }
    }
#endif
};

static
FdRegistry* fds;

void fd_registry_init()
{
    fds = new FdRegistry {};
}

void fd_registry_deinit()
{
    delete fds;
}

auto fd_get_ref_count(fd_t fd) -> u32
{
    if (!fd_is_valid(fd)) return 0;

    return fds->ref_counts[fd];
}

#define NOISY_FDS 0

#if NOISY_FDS
#define FD_LOG(...) log_debug(__VA_ARGS__)
#else
#define FD_LOG(...)
#endif

auto fd_add_ref(fd_t fd) -> fd_t
{
    if (!fd_is_valid(fd)) return -1;

    FD_LOG("fd_add_ref({}) {} -> {}", fd, fds->ref_counts[fd], fds->ref_counts[fd] + 1);
    fds->ref_counts[fd]++;
    return fd;
}

static
void destroy_fd(fd_t fd)
{
    FD_LOG("  close({})", fd);
    // std::cout << std::stacktrace::current();
    unix_check<close>(fd);
}

auto fd_remove_ref(fd_t fd) -> fd_t
{
    if (!fd_is_valid(fd)) return -1;

    FD_LOG("fd_remove_ref({}) {} -> {}", fd, fds->ref_counts[fd], fds->ref_counts[fd] - 1);
    if (!--fds->ref_counts[fd]) {
        destroy_fd(fd);
        return -1;
    }

    return fd;
}

auto fd_extract(fd_t fd) -> fd_t
{
    debug_assert(fd_is_valid(fd));
    debug_assert(fd_get_ref_count(fd) == 1);
    fds->ref_counts[fd] = 0;
    return fd;
}

auto Fd::extract() noexcept -> fd_t
{
    return fd_extract(std::exchange(fd, -1));
}
