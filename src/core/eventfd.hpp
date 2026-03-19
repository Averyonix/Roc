#pragma once

#include "types.hpp"
#include "debug.hpp"

inline
u64 core_eventfd_read(int fd)
{
    u64 count = 0;
    return (unix_check<read, EAGAIN, EINTR>(fd, &count, sizeof(count)).value == sizeof(count)) ? count : 0;
}

inline
void core_eventfd_signal(int fd, u64 inc)
{
    unix_check<write>(fd, &inc, sizeof(inc));
}