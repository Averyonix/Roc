#include "internal.hpp"

WREI_OBJECT_EXPLICIT_DEFINE(wrio_context);

auto wrio_context_create() -> ref<wrio_context>
{
    auto ctx = wrei_create<wrio_context>();

    ctx->event_loop = wrei_event_loop_create();

    wrio_session_init( ctx.get());
    wrio_libinput_init(ctx.get());
    wrio_evdev_init(   ctx.get());
    wrio_drm_init(     ctx.get());
    wrio_wayland_init( ctx.get());

    ctx->wren = wren_create({}, ctx->event_loop.get(), -1);

    return ctx;
}

void wrio_context_run(wrio_context* ctx)
{
    wrei_event_loop_run(ctx->event_loop.get());
}
