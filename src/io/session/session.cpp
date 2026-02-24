#include "../internal.hpp"

struct io_session
{
};

CORE_OBJECT_EXPLICIT_DEFINE(io_session);

void io_session_init(io_context*)
{
    log_error("IO - session backend not implemented");
}
