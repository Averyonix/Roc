#include "internal.hpp"

CORE_OBJECT_EXPLICIT_DEFINE(io_context);

auto io_create(core_event_loop* event_loop, gpu_context* gpu) -> ref<io_context>
{
    auto ctx = core_create<io_context>();

    ctx->event_loop = event_loop;
    ctx->gpu = gpu;

    io_session_init( ctx.get());
    io_libinput_init(ctx.get());
    io_evdev_init(   ctx.get());
    io_drm_init(     ctx.get());
    io_wayland_init( ctx.get());

    return ctx;
}

io_context::~io_context()
{
}

void io_set_event_handler(io_context* ctx, std::move_only_function<io_event_handler>&& handler)
{
    ctx->event_handler = std::move(handler);
}

static
weak<io_context> signal_context;

static
void signal_handler(int sig)
{
    if (sig == SIGINT) {
        // Immediately unregister SIGINT in case of unresponsive event loop
        std::signal(sig, SIG_DFL);
    }

    if (!signal_context) return;

    switch (sig) {
        break;case SIGTERM: io_request_shutdown(signal_context.get(), io_shutdown_reason::terminate_receieved);
        break;case SIGINT:  io_request_shutdown(signal_context.get(), io_shutdown_reason::interrupt_receieved);
    }
}

void io_run(io_context* ctx)
{
    core_assert(ctx->event_handler);

    signal_context = ctx;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (ctx->wayland) {
        io_wayland_start(ctx);
    }

    core_event_loop_run(ctx->event_loop);

    signal_context = nullptr;
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_IGN);
}

void io_request_shutdown(io_context* ctx, io_shutdown_reason reason)
{
    core_event_loop_enqueue(ctx->event_loop, [ctx, reason] {
        io_post_event(ptr_to(io_event {
            .ctx = ctx,
            .type = io_event_type::shutdown_requested,
            .shutdown {
                .reason = reason,
            }
        }));
    });
}

void io_stop(io_context* ctx)
{
    core_event_loop_stop(ctx->event_loop);
}

void io_post_event(io_event* event)
{
    event->ctx->event_handler(event);
}
