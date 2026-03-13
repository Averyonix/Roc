#pragma once

CORE_NOINLINE inline
void core_debugbreak()
{
    std::cerr << std::stacktrace::current() << std::endl;
    raise(SIGTRAP);
}

[[noreturn]] CORE_NOINLINE inline
void core_debugkill()
{
    std::cerr << std::stacktrace::current() << std::endl;
    std::terminate();
}

[[noreturn]] inline
void core_unreachable()
{
#ifdef NDEBUG
    std::unreachable();
#else
    core_debugkill();
#endif
}

[[noreturn]] CORE_NOINLINE
void core_assert_fail(std::string_view expr, std::string_view reason = {});

#define core_assert(Expr, ...) \
    (static_cast<bool>(Expr) ? void() : core_assert_fail(#Expr __VA_OPT__(, std::format(__VA_ARGS__))))

// -----------------------------------------------------------------------------
//      UNIX error checking
// -----------------------------------------------------------------------------

void core_log_unix_error(std::string_view message, int err = 0);

template<typename T>
struct core_unix_result
{
    T   value;
    int error;

    bool ok()  const noexcept { return !error; }
    bool err() const noexcept { return  error; }
};

template<typename T>
    requires std::is_integral_v<T> || std::is_pointer_v<T>
auto core_unix_check(T value, auto... quiet) -> core_unix_result<T>
{
    static constexpr int fallback_error_code = INT_MAX;
    int error_code = 0;
    if constexpr (std::is_signed_v<T>) {
        if (value < 0) {
            if (value == -1 && errno) {
                // TODO: This has a failure case where `errno` is set spuriously when
                //       EPERM was the intended error code
                error_code = errno;
            } else {
                error_code = -int(value);
            }
        }
    } else if (!value) {
        error_code = errno ?: fallback_error_code;
    }

    if (!error_code || (... || (error_code == quiet))) [[likely]] {
        return { value, error_code };
    }

    core_log_unix_error("core_unix_check", error_code);

    return { value, error_code };
}

#define unix_check(Expr, ...) \
    core_unix_check((errno = 0, (Expr)) __VA_OPT__(,) __VA_ARGS__)

/**
 * Custom error checking logic is required for `mmap` as it doesn't follow the usual
 * error cases (-1 for integer types, falsey for unsigned / pointers).
 * Failed maps instead return `MAP_FAILED` : `((void*)-1)`
 */
inline
auto core_mmap(void* addr, size_t len, int prot, int flags, int fd, __off_t offset) -> core_unix_result<void*>
{
    auto map = mmap(addr, len, prot, flags, fd, offset);
    if (map != MAP_FAILED) return { map, 0 };

    auto error_code = errno;
    core_log_unix_error("core_mmap", error_code);
    return { nullptr, error_code };
}
