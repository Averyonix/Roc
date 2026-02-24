#include "../internal.hpp"

struct wrio_drm
{
};

WREI_OBJECT_EXPLICIT_DEFINE(wrio_drm);

void wrio_drm_init(wrio_context*)
{
    log_error("WRIO - drm backend not implemented");
}
