#include "../internal.hpp"

struct IoDrm
{
};

void io_drm_init(IoContext*)
{
    log_error("IO - drm backend not implemented");
}

void io_drm_deinit(IoContext*) {}
