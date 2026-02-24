#include "../internal.hpp"

struct wrio_libinput
{
};

WREI_OBJECT_EXPLICIT_DEFINE(wrio_libinput);

void wrio_libinput_init(wrio_context*)
{
    log_error("WRIO - libinput backend not implemented");
}
