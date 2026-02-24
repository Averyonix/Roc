#include "../internal.hpp"

struct io_drm
{
};

CORE_OBJECT_EXPLICIT_DEFINE(io_drm);

void io_drm_init(io_context*)
{
    log_error("IO - drm backend not implemented");
}
