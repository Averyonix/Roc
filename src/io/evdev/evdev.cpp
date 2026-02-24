#include "../internal.hpp"

struct io_evdev
{
};

CORE_OBJECT_EXPLICIT_DEFINE(io_evdev);

void io_evdev_init(io_context*)
{
    log_error("IO_ - evdev backend not implemented");
}
