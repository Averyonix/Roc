#pragma once

#include "../util.hpp"

#include <gpu/gpu.hpp>

#include <wayland/server/linux-dmabuf-v1.h>
#include <wayland/server/linux-drm-syncobj-v1.h>

struct WayDamageRegion;
struct WayServer;
struct WaySurface;

struct WayBuffer : WayObject
{
    vec2u32 extent;

    // Sent on apply, should return a GpuImage when the buffer is ready to display
    [[nodiscard]] virtual auto acquire(WaySurface*, WayDamageRegion) -> Ref<GpuImage> = 0;

protected:
    ~WayBuffer() = default;
};

// -----------------------------------------------------------------------------

struct WayShmPool : WayObject
{
    WayServer* server;

    WayResource resource;

    Fd    fd;
    void* data;
    usz   size;

    ~WayShmPool();
};

// -----------------------------------------------------------------------------

void way_dmabuf_init(WayServer*);

// -----------------------------------------------------------------------------

WAY_INTERFACE_DECLARE(wl_buffer);

WAY_INTERFACE_DECLARE(wl_shm, 2);
WAY_INTERFACE_DECLARE(wl_shm_pool);

WAY_INTERFACE_DECLARE(zwp_linux_dmabuf_v1, 5);
WAY_INTERFACE_DECLARE(zwp_linux_buffer_params_v1);
WAY_INTERFACE_DECLARE(zwp_linux_dmabuf_feedback_v1);
