#include "wayland.hpp"

WREI_OBJECT_EXPLICIT_DEFINE(wrio_wayland);

// -----------------------------------------------------------------------------

static
void registry_global(void* data, wl_registry*, u32 name, const char* interface, u32 version)
{
    auto* ctx = static_cast<wrio_context*>(data);
    auto* wl = ctx->wayland.get();

    auto match_interface = [&](const wl_interface* wl_interface, auto member) -> bool {
        if (strcmp(interface, wl_interface->name) != 0) {
            return false;
        }

        u32 bound_version = std::min(version, u32(wl_interface->version));

        wrei_assert(!(wl->*member), "Interface <{}> already bound", interface);
        wl->*member = static_cast<std::remove_cvref_t<decltype(wl->*member)>>(
            wl_registry_bind(wl->wl_registry, name, wl_interface, bound_version));

        log_info("wl_global[{:2} : {:41}], version = {} (bound: {})", name, interface, version, bound_version);

        return true;
    };

#define BIND_BEGIN                if (false) {}
#define BIND_INTERFACE(Interface) else if (match_interface(&Interface##_interface, &wrio_wayland::Interface))
#define BIND_END                  else log_trace("wl_global[{:2} : {:41}], version = {}", name, interface, version);

    BIND_BEGIN
    BIND_INTERFACE(wl_compositor) {}
    BIND_INTERFACE(xdg_wm_base) {
        xdg_wm_base_add_listener(wl->xdg_wm_base, &wrio_xdg_wm_base_listener, ctx);
    }
    BIND_INTERFACE(zxdg_decoration_manager_v1) {}
    BIND_INTERFACE(wl_seat) {
        wl_seat_add_listener(wl->wl_seat, &wrio_wl_seat_listener, ctx);
    }
    BIND_INTERFACE(zwp_relative_pointer_manager_v1){}
    BIND_INTERFACE(zwp_pointer_constraints_v1) {}
    BIND_INTERFACE(zwp_linux_dmabuf_v1) {
        auto feedback = zwp_linux_dmabuf_v1_get_default_feedback(wl->zwp_linux_dmabuf_v1);
        zwp_linux_dmabuf_feedback_v1_add_listener(feedback, &wrio_zwp_linux_dmabuf_feedback_v1_listener, ctx);
    }
    BIND_INTERFACE(wp_linux_drm_syncobj_manager_v1) {}
    BIND_END

#undef BIND_BEGIN
#undef BIND_INTERFACE
#undef BIND_END
}

WRIO_WL_LISTENER(wl_registry) = {
    .global = registry_global,
    WRIO_WL_STUB(wl_registry, global_remove),
};

// -----------------------------------------------------------------------------

WRIO_WL_LISTENER(xdg_wm_base) = {
    .ping = [](void*, xdg_wm_base* base, u32 serial) {
        xdg_wm_base_pong(base, serial);
    }
};

// -----------------------------------------------------------------------------

static
void display_read(wrio_context* ctx, wrei_fd_event_bits events)
{
    ctx->wayland->current_dispatch_time = std::chrono::steady_clock::now();

    timespec timeout = {};
    if (unix_check(wl_display_dispatch_timeout(ctx->wayland->wl_display, &timeout)).err()) {
        wrei_debugkill();
    }

    wl_display_flush(ctx->wayland->wl_display);
}

void wrio_wayland_init(wrio_context* ctx)
{
    ctx->wayland = wrei_create<wrio_wayland>();
    auto* wl = ctx->wayland.get();

    wl->wl_display = wl_display_connect(nullptr);

    wl->wl_registry = wl_display_get_registry(wl->wl_display);
    wl_registry_add_listener(wl->wl_registry, &wrio_wl_registry_listener, ctx);

    // First roundtrip binds interfaces
    wl_display_roundtrip(wl->wl_display);

    // Second roundtrip ensure that all events expected in response to binding are received
    wl_display_roundtrip(wl->wl_display);

    wl->wl_display_fd = wrei_fd_reference(wl_display_get_fd(wl->wl_display));
    wrei_fd_set_listener(wl->wl_display_fd.get(), ctx->event_loop.get(), wrei_fd_event_bit::readable,
        [ctx = weak(ctx)](wrei_fd*, wrei_fd_event_bits events) {
            if (ctx) display_read(ctx.get(), events);
        });

    wrio_context_add_output(ctx);
}

wrio_wayland::~wrio_wayland()
{
    if (keyboard) keyboard = nullptr;
    if (pointer)  pointer = nullptr;

    wl_display_fd = nullptr;

    outputs.clear();

    zwp_linux_dmabuf_v1_destroy(zwp_linux_dmabuf_v1);
    syncobj_cache.entries.clear();
    buffer_cache.entries.clear();
    wp_linux_drm_syncobj_manager_v1_destroy(wp_linux_drm_syncobj_manager_v1);

    zwp_relative_pointer_manager_v1_destroy(zwp_relative_pointer_manager_v1);
    zwp_pointer_constraints_v1_destroy(zwp_pointer_constraints_v1);

    if (zxdg_decoration_manager_v1) zxdg_decoration_manager_v1_destroy(zxdg_decoration_manager_v1);
    wl_compositor_destroy(wl_compositor);
    xdg_wm_base_destroy(xdg_wm_base);
    wl_seat_destroy(wl_seat);

    wl_registry_destroy(wl_registry);

    wl_display_disconnect(wl_display);
}
