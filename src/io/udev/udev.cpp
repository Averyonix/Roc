#include "../internal.hpp"

struct wrio_udev
{
};

WREI_OBJECT_EXPLICIT_DEFINE(wrio_udev);

void wrio_udev_init(wrio_context*)
{
    log_error("WRIO - udev backend not implemented");
}
