#include "pch.hpp"
#include "log.hpp"

using namespace std::literals;

#define VT_color_begin(color) "\u001B[" #color "m"
#define VT_color_reset "\u001B[0m"
#define VT_color(color, text) VT_color_begin(color) text VT_color_reset

static struct {
    LogLevel log_level = LogLevel::trace;
    std::ofstream log_file;
    MessageConnection* ipc_sink = {};
} log_state = {};

void log_set_message_sink(struct MessageConnection* conn)
{
    log_state.ipc_sink = conn;
}

LogLevel get_log_level()
{
    return log_state.log_level;
}

void log(LogLevel level, std::string_view message)
{
    if (log_state.log_level > level) return;

    struct {
        const char* vt;
        const char* plain;
    } fmt;

    switch (level) {
        case LogLevel::trace: fmt = { "[" VT_color(90, "TRACE") "] " VT_color(90, "{}") "\n", "[TRACE] {}\n" }; break;
        case LogLevel::debug: fmt = { "[" VT_color(96, "DEBUG") "] {}\n",                     "[DEBUG] {}\n" }; break;
        case LogLevel::info:  fmt = { " [" VT_color(94, "INFO") "] {}\n",                     " [INFO] {}\n" }; break;
        case LogLevel::warn:  fmt = { " [" VT_color(93, "WARN") "] {}\n",                     " [WARN] {}\n" }; break;
        case LogLevel::error: fmt = { "[" VT_color(91, "ERROR") "] {}\n",                     "[ERROR] {}\n" }; break;
        case LogLevel::fatal: fmt = { "[" VT_color(91, "FATAL") "] {}\n",                     "[FATAL] {}\n" }; break;
    }

    std::cout << std::vformat(fmt.vt, std::make_format_args(message));
    if (log_state.log_file.is_open()) {
        log_state.log_file << std::vformat(fmt.plain, std::make_format_args(message)) << std::flush;
    }
}

void init_log(LogLevel log_level,  const char* log_file)
{
    log_state.log_level = log_level;
    if (log_file) log_state.log_file = std::ofstream(log_file);
}
