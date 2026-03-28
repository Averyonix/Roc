#include "../internal.hpp"

struct IoSession
{
};

void io_session_init(IoContext*)
{
    log_error("IO - session backend not implemented");
}

void io_session_deinit(IoContext*) {}
