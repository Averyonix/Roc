#include "../internal.hpp"

struct IoEvdev
{
};

void io_evdev_init(IoContext*)
{
    log_error("IO - evdev backend not implemented");
}

void io_evdev_deinit(IoContext*) {}
