#include "../internal.hpp"

struct IoLibinput
{
};

void io_libinput_init(IoContext*)
{
    log_error("IO - libinput backend not implemented");
}

void io_libinput_deinit(IoContext*) {}
