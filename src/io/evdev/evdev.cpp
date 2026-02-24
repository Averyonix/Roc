#include "../internal.hpp"

struct wrio_evdev
{
};

WREI_OBJECT_EXPLICIT_DEFINE(wrio_evdev);

void wrio_evdev_init(wrio_context*)
{
    log_error("WRIO - evdev backend not implemented");
}
