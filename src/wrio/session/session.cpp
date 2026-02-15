#include "../internal.hpp"

struct wrio_session
{
};

WREI_OBJECT_EXPLICIT_DEFINE(wrio_session);

void wrio_session_init(wrio_context*)
{
    log_error("WRIO - session backend not implemented");
}
