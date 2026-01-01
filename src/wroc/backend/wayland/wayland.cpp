#include "backend.hpp"

// -----------------------------------------------------------------------------

static
void wroc_listen_xdg_wm_base_ping(void*, struct xdg_wm_base* xdg_wm_base, u32 serial)
{
    log_trace("xdg_wm_base::ping(serial = {})", serial);

    xdg_wm_base_pong(xdg_wm_base, serial);
}

const xdg_wm_base_listener wroc_xdg_wm_base_listener = {
    .ping = wroc_listen_xdg_wm_base_ping,
};

// -----------------------------------------------------------------------------

static
void wroc_listen_registry_global(void* data, wl_registry*, u32 name, const char* interface, u32 version)
{
    auto* backend = static_cast<wroc_wayland_backend*>(data);

    bool matched;

#define IF_BIND_INTERFACE(Interface, Member, ...) \
    matched = strcmp(interface, Interface.name) == 0; \
    if (matched) { \
        u32 wroc_version_bind_global = std::min(version, u32(Interface.version)); \
        backend->Member = static_cast<decltype(backend->Member)>(wl_registry_bind(backend->wl_registry, name, &Interface, wroc_version_bind_global)); \
        log_info("wl_registry::global(name = {:2}, interface = {:41}, version = {:2} ({}))", name, interface, version, wroc_version_bind_global); \
        { __VA_ARGS__ } \
        break; \
    }

    do {
        IF_BIND_INTERFACE(wl_compositor_interface, wl_compositor)
        IF_BIND_INTERFACE(xdg_wm_base_interface, xdg_wm_base, {
            xdg_wm_base_add_listener(backend->xdg_wm_base, &wroc_xdg_wm_base_listener, backend);
        })
        IF_BIND_INTERFACE(zxdg_decoration_manager_v1_interface, decoration_manager)
        IF_BIND_INTERFACE(wl_seat_interface, seat, {
            wl_seat_add_listener(backend->seat, &wroc_wl_seat_listener, backend);
        })
#if WROC_BACKEND_RELATIVE_POINTER
        IF_BIND_INTERFACE(zwp_relative_pointer_manager_v1_interface, relative_pointer_manager);
        IF_BIND_INTERFACE(zwp_pointer_constraints_v1_interface, pointer_constraints);
#endif

        log_trace("wl_registry::global(name = {:2}, interface = {:41}, version = {:2})", name, interface, version);
    } while (false);

#undef IF_BIND_INTERFACE
}

static
void wroc_listen_registry_global_remove(void*, wl_registry*, u32 name)
{
    log_warn("wl_registry::global_remove(name = {:2})", name);
}

const wl_registry_listener wroc_wl_registry_listener {
    .global        = wroc_listen_registry_global,
    .global_remove = wroc_listen_registry_global_remove,
};

static
int wroc_listen_backend_display_read(wroc_wayland_backend* backend, int fd, u32 mask)
{
    timespec timeout = {};
    if (wl_display_dispatch_timeout(backend->wl_display, &timeout) == -1) {
        log_error("  wl_display_read_events failed: {}", strerror(errno));
        backend->event_source = nullptr;
        return false;
    }
    wl_display_flush(backend->wl_display);

    return 1;
}

void wroc_wayland_backend_init(wroc_server* server)
{
    auto backend = wrei_create<wroc_wayland_backend>();
    server->backend = backend;
    backend->server = server;

    if (getenv("WROC_WAYLAND_DEBUG_BACKEND")) {
        setenv("WAYLAND_DEBUG", "1", true);
    } else {
        unsetenv("WAYLAND_DEBUG");
    }
    backend->wl_display = wl_display_connect(nullptr);
    unsetenv("WAYLAND_DEBUG");
    backend->wl_registry = wl_display_get_registry(backend->wl_display);

    wl_registry_add_listener(backend->wl_registry, &wroc_wl_registry_listener, backend.get());
    wl_display_roundtrip(backend->wl_display);

    backend->event_source = wrei_event_loop_add_fd(server->event_loop.get(), wl_display_get_fd(backend->wl_display), EPOLLIN,
        [backend = backend.get()](int fd, u32 events) {

            wroc_listen_backend_display_read(backend, fd, events);
        });

    backend->create_output();

    wl_display_flush(backend->wl_display);
}

wroc_wayland_backend::~wroc_wayland_backend()
{
    if (keyboard) keyboard = nullptr;
    if (pointer)  pointer = nullptr;

    event_source = nullptr;

    outputs.clear();

#if WROC_BACKEND_RELATIVE_POINTER
    zwp_relative_pointer_manager_v1_destroy(relative_pointer_manager);
    zwp_pointer_constraints_v1_destroy(pointer_constraints);
#endif

    zxdg_decoration_manager_v1_destroy(decoration_manager);
    wl_compositor_destroy(wl_compositor);
    xdg_wm_base_destroy(xdg_wm_base);
    wl_seat_destroy(seat);

    wl_registry_destroy(wl_registry);

    wl_display_disconnect(wl_display);
}
