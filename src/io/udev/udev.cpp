#include "../internal.hpp"

struct IoUdev
{
};

void io_udev_init(IoContext*)
{
    log_error("IO - udev backend not implemented");
}

void io_udev_deinit(IoContext*) {}
