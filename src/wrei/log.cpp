#include "pch.hpp"
#include "log.hpp"
#include "util.hpp"

#define WREI_VT_COLOR_BEGIN(color) "\u001B[" #color "m"
#define WREI_VT_COLOR_RESET "\u001B[0m"
#define WREI_VT_COLOR(color, text) WREI_VT_COLOR_BEGIN(color) text WREI_VT_COLOR_RESET

static struct {
    wrei_log_level log_level = wrei_log_level::trace;
    std::ofstream log_file;
    MessageConnection* ipc_sink = {};

    struct {
        std::string buffer;
        std::vector<wrei_log_entry> entries;
        u32 lines;
        bool enabled;
    } history;

    wrei_stacktrace_cache stacktraces;
    std::recursive_mutex mutex;
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

bool wrei_log_is_history_enabled()
{
    std::scoped_lock _ { wrei_log_state.mutex };

    return wrei_log_state.history.enabled;
}

void wrei_log_set_history_enabled(bool enabled)
{
    std::scoped_lock _ { wrei_log_state.mutex };

    wrei_log_state.history.enabled = enabled;
}

void wrei_log_clear_history()
{
    std::scoped_lock _ { wrei_log_state.mutex };

    wrei_log_state.history.buffer.clear();
    wrei_log_state.history.entries.clear();
    wrei_log_state.history.lines = 0;
}

wrei_log_history wrei_log_get_history()
{
    std::unique_lock lock { wrei_log_state.mutex };
    return { std::move(lock), wrei_log_state.history.entries, wrei_log_state.history.lines };
}

std::string_view wrei_log_entry::message() const noexcept
{
    return std::string_view(wrei_log_state.history.buffer).substr(start, len);
}

const wrei_log_entry* wrei_log_history::find(u32 line) const noexcept
{
    auto& state = wrei_log_state;

    struct Compare
    {
        bool operator()(const wrei_log_entry& entry, u32 line)
        {
            return entry.line_start < line;
        }

        bool operator()(u32 line, const wrei_log_entry& entry)
        {
            return line < entry.line_start;
        }
    };

    if (state.history.entries.empty()) return nullptr;

    auto iter = std::upper_bound(state.history.entries.begin(), state.history.entries.end(), line, Compare{});

    // upper_bound will never return begin() in a non-empty list as:
    //  1) line >= 0
    //  2) the first entry will *always* have line_start == 0
    //  3) upper_bound will return the first entry *greater* than line
    // Hence, iter[-1] is safe here
    return &iter[-1];
}

void wrei_log(wrei_log_level level, std::string_view message)
{
    auto& state = wrei_log_state;

    std::scoped_lock _ { state.mutex };

    if (state.log_level > level) return;

    auto timestamp = wrei_time_current();

    if (state.history.enabled) {
        auto[stacktrace, added] = state.stacktraces.insert(std::stacktrace::current(1));

        auto start = state.history.buffer.size();
        state.history.buffer.append(message);
        auto lines = u32(std::ranges::count(message, '\n') + 1);
        state.history.entries.emplace_back(wrei_log_entry {
            .level = level,
            .timestamp = timestamp,
            .start = u32(start),
            .len = u32(message.size()),
            .line_start = state.history.lines,
            .lines = lines,
            .stacktrace = stacktrace,
        });
        state.history.lines += lines;
    }

    struct {
        const char* vt;
        const char* plain;
    } fmt;

    switch (level) {
        break;case wrei_log_level::trace:
            fmt = { WREI_VT_COLOR(90, "{}") " [" WREI_VT_COLOR(90, "TRACE") "] " WREI_VT_COLOR(90, "{}") "\n", "{} [TRACE] {}\n" };
        break;case wrei_log_level::debug:
            fmt = { WREI_VT_COLOR(90, "{}") " [" WREI_VT_COLOR(96, "DEBUG") "] {}\n",                          "{} [DEBUG] {}\n" };
        break;case wrei_log_level::info:
            fmt = { WREI_VT_COLOR(90, "{}") "  [" WREI_VT_COLOR(94, "INFO") "] {}\n",                          "{}  [INFO] {}\n" };
        break;case wrei_log_level::warn:
            fmt = { WREI_VT_COLOR(90, "{}") "  [" WREI_VT_COLOR(93, "WARN") "] {}\n",                          "{}  [WARN] {}\n" };
        break;case wrei_log_level::error:
            fmt = { WREI_VT_COLOR(90, "{}") " [" WREI_VT_COLOR(91, "ERROR") "] {}\n",                          "{} [ERROR] {}\n" };
        break;case wrei_log_level::fatal:
            fmt = { WREI_VT_COLOR(90, "{}") " [" WREI_VT_COLOR(91, "FATAL") "] {}\n",                          "{} [FATAL] {}\n" };
    }

    auto time_str = wrei_time_to_string(timestamp, wrei_time_format::time_ms);
    std::cout << std::vformat(fmt.vt, std::make_format_args(time_str, message));
    if (state.log_file.is_open()) {
        auto datetime_str = wrei_time_to_string(timestamp, wrei_time_format::datetime_ms);
        state.log_file << std::vformat(fmt.plain, std::make_format_args(datetime_str, message)) << std::flush;
    }
}

void wrei_init_log(wrei_log_level log_level,  const char* log_file)
{
    wrei_log_state.log_level = log_level;
    if (log_file) wrei_log_state.log_file = std::ofstream(log_file);
}
