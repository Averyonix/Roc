#include "debug.hpp"
#include "log.hpp"

[[noreturn]] CORE_NOINLINE
void debug_assert_fail(std::string_view expr, std::string_view reason)
{
    log_error("assert({}) failed{}{}", expr, reason.empty() ? "" : ": ", reason);
    debug_kill();
}

void log_unix_error(std::string_view message, unix_error_t err)
{
    err = err ?: errno;

    if (message.empty()) { log_error("({}) {}",              err, strerror(err)); }
    else                 { log_error("{}: ({}) {}", message, err, strerror(err)); }
}
