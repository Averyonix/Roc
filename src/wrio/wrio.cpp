#include "internal.hpp"

WREI_OBJECT_EXPLICIT_DEFINE(wrio_context);

auto wrio_context_create(wrei_event_loop* event_loop, wren_context* wren) -> ref<wrio_context>
{
    auto ctx = wrei_create<wrio_context>();

    ctx->event_loop = event_loop;
    ctx->wren = wren;

    wrio_session_init( ctx.get());
    wrio_libinput_init(ctx.get());
    wrio_evdev_init(   ctx.get());
    wrio_drm_init(     ctx.get());
    wrio_wayland_init( ctx.get());

    return ctx;
}

wrio_context::~wrio_context()
{
}

void wrio_context_set_event_handler(wrio_context* ctx, std::move_only_function<wrio_event_handler>&& handler)
{
    ctx->event_handler = std::move(handler);
}

static
weak<wrio_context> signal_context;

static
void signal_handler(int sig)
{
    if (sig == SIGINT) {
        // Immediately unregister SIGINT in case of unresponsive event loop
        std::signal(sig, SIG_DFL);
    }

    if (!signal_context) return;

    switch (sig) {
        break;case SIGTERM:
            wrio_context_request_shutdown(signal_context.get(), wrio_shutdown_reason::terminate_receieved);
        break;case SIGINT:
            wrio_context_request_shutdown(signal_context.get(), wrio_shutdown_reason::interrupt_receieved);
    }
}

void wrio_context_run(wrio_context* ctx)
{
    wrei_assert(ctx->event_handler);

    signal_context = ctx;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (ctx->wayland) {
        wrio_wayland_start(ctx);
    }

    wrei_event_loop_run(ctx->event_loop);

    signal_context = nullptr;
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_IGN);
}

void wrio_context_request_shutdown(wrio_context* ctx, wrio_shutdown_reason reason)
{
    wrei_event_loop_enqueue(ctx->event_loop, [ctx, reason] {
        wrio_post_event(wrei_ptr_to(wrio_event {
            .ctx = ctx,
            .type = wrio_event_type::shutdown_requested,
            .shutdown {
                .reason = reason,
            }
        }));
    });
}

void wrio_context_stop(wrio_context* ctx)
{
    wrei_event_loop_stop(ctx->event_loop);
}

void wrio_post_event(wrio_event* event)
{
    event->ctx->event_handler(event);
}
