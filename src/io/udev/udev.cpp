#include "../internal.hpp"

struct io_udev
{
};

void io_udev_init(io_context*)
{
    log_error("IO - udev backend not implemented");
}

void io_udev_deinit(io_context*) {}
