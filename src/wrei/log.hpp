#pragma once

#include "pch.hpp"
#include "types.hpp"

enum class wrei_log_level : u32
{
    trace,
    debug,
    info,
    warn,
    error,
    fatal,
};

void wrei_log_set_message_sink(struct MessageConnection*);
wrei_log_level wrei_get_log_level();
bool wrei_is_log_level_enabled(wrei_log_level);
void wrei_init_log(wrei_log_level, const char* log_file);
void      wrei_log(wrei_log_level, std::string_view message);

struct wrei_log_entry
{
    wrei_log_level level;
    std::string message;
};
struct wrei_log_history : std::span<const wrei_log_entry>
{
    std::unique_lock<std::recursive_mutex> mutex;
};
wrei_log_history wrei_log_get_history();
void wrei_log_set_history_enabled(bool enabled);
bool wrei_log_is_history_enabled();
void wrei_log_clear_history();

template<typename ...Args>
void wrei_log(wrei_log_level level, std::format_string<Args...> fmt, Args&&... args)
{
    if (wrei_get_log_level() > level) return;
    wrei_log(level, std::vformat(fmt.get(), std::make_format_args(args...)));
}

#define log_trace(fmt, ...) if (wrei_is_log_level_enabled(wrei_log_level::trace)) wrei_log(wrei_log_level::trace, std::format(fmt __VA_OPT__(,) __VA_ARGS__))
#define log_debug(fmt, ...) if (wrei_is_log_level_enabled(wrei_log_level::debug)) wrei_log(wrei_log_level::debug, std::format(fmt __VA_OPT__(,) __VA_ARGS__))
#define log_info( fmt, ...) if (wrei_is_log_level_enabled(wrei_log_level::info )) wrei_log(wrei_log_level::info,  std::format(fmt __VA_OPT__(,) __VA_ARGS__))
#define log_warn( fmt, ...) if (wrei_is_log_level_enabled(wrei_log_level::warn )) wrei_log(wrei_log_level::warn,  std::format(fmt __VA_OPT__(,) __VA_ARGS__))
#define log_error(fmt, ...) if (wrei_is_log_level_enabled(wrei_log_level::error)) wrei_log(wrei_log_level::error, std::format(fmt __VA_OPT__(,) __VA_ARGS__))
