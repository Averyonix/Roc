#include "pch.hpp"
#include "log.hpp"

using namespace std::literals;

#define WREI_VT_COLOR_BEGIN(color) "\u001B[" #color "m"
#define WREI_VT_COLOR_RESET "\u001B[0m"
#define WREI_VT_COLOR(color, text) WREI_VT_COLOR_BEGIN(color) text WREI_VT_COLOR_RESET

static struct {
    wrei_log_level log_level = wrei_log_level::trace;
    std::ofstream log_file;
    MessageConnection* ipc_sink = {};
} wrei_log_state = {};

void wrei_log_set_message_sink(struct MessageConnection* conn)
{
    wrei_log_state.ipc_sink = conn;
}

wrei_log_level wrei_get_log_level()
{
    return wrei_log_state.log_level;
}

bool wrei_is_log_level_enabled(wrei_log_level level)
{
    return level >= wrei_get_log_level();
}

void wrei_log(wrei_log_level level, std::string_view message)
{
    if (wrei_log_state.log_level > level) return;

    struct {
        const char* vt;
        const char* plain;
    } fmt;

    switch (level) {
        case wrei_log_level::trace: fmt = { "[" WREI_VT_COLOR(90, "TRACE") "] " WREI_VT_COLOR(90, "{}") "\n", "[TRACE] {}\n" }; break;
        case wrei_log_level::debug: fmt = { "[" WREI_VT_COLOR(96, "DEBUG") "] {}\n",                     "[DEBUG] {}\n" }; break;
        case wrei_log_level::info:  fmt = { " [" WREI_VT_COLOR(94, "INFO") "] {}\n",                     " [INFO] {}\n" }; break;
        case wrei_log_level::warn:  fmt = { " [" WREI_VT_COLOR(93, "WARN") "] {}\n",                     " [WARN] {}\n" }; break;
        case wrei_log_level::error: fmt = { "[" WREI_VT_COLOR(91, "ERROR") "] {}\n",                     "[ERROR] {}\n" }; break;
        case wrei_log_level::fatal: fmt = { "[" WREI_VT_COLOR(91, "FATAL") "] {}\n",                     "[FATAL] {}\n" }; break;
    }

    std::cout << std::vformat(fmt.vt, std::make_format_args(message));
    if (wrei_log_state.log_file.is_open()) {
        wrei_log_state.log_file << std::vformat(fmt.plain, std::make_format_args(message)) << std::flush;
    }
}

void wrei_init_log(wrei_log_level log_level,  const char* log_file)
{
    wrei_log_state.log_level = log_level;
    if (log_file) wrei_log_state.log_file = std::ofstream(log_file);
}
