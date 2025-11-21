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
void wroc_listen_registry_global(void *data, wl_registry*, u32 name, const char* interface, u32 version)
{
    auto* backend = static_cast<wroc_backend*>(data);

    bool matched;

#define IF_BIND_INTERFACE(Interface, Member, ...) \
    matched = strcmp(interface, Interface.name) == 0; \
    if (matched) { \
        u32 wroc_version_bind_global = std::min(version, u32(Interface.version)); \
        backend->Member = static_cast<decltype(backend->Member)>(wl_registry_bind(backend->wl_registry, name, &Interface, wroc_version_bind_global)); \
        log_info("wl_registry::global(name = {:2}, interface = {:41}, version = {:2} ({}))", name, interface, version, wroc_version_bind_global); \
        { __VA_ARGS__ }\
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

        log_trace("wl_registry::global(name = {:2}, interface = {:41}, version = {:2})", name, interface, version);
    } while (false);

#undef IF_BIND_INTERFACE
}

static
void wroc_listen_registry_global_remove(void* /* data */, wl_registry*, u32 name)
{
    log_warn("wl_registry::global_remove(name = {:2})", name);
}

const wl_registry_listener wroc_wl_registry_listener {
    .global        = wroc_listen_registry_global,
    .global_remove = wroc_listen_registry_global_remove,
};

static
int wroc_listen_backend_display_read(int fd, u32 mask, void* data)
{
    auto* backend = static_cast<wroc_backend*>(data);

    // log_trace("backend display read, events = {:#x}", events);

    timespec timeout = {};
    int res = wl_display_dispatch_timeout(backend->wl_display, &timeout);
    if (res < 0) {
        log_error("  wl_display_dispatch: {}", res);
        wl_event_source_remove(backend->event_source);
        backend->event_source = nullptr;
    }

    wl_display_flush(backend->wl_display);

    return 1;
}

void wroc_backend_init(wroc_server* server)
{
    auto* backend = wrei_get_registry(server)->create<wroc_backend>();
    backend->server = server;

    if (getenv("WROC_WAYLAND_DEBUG_BACKEND")) {
        setenv("WAYLAND_DEBUG", "1", true);
    } else {
        unsetenv("WAYLAND_DEBUG");
    }
    backend->wl_display = wl_display_connect(nullptr);
    unsetenv("WAYLAND_DEBUG");
    backend->wl_registry = wl_display_get_registry(backend->wl_display);

    wl_registry_add_listener(backend->wl_registry, &wroc_wl_registry_listener, backend);
    wl_display_roundtrip(backend->wl_display);

    server->backend = backend;

    backend->event_source = wl_event_loop_add_fd(server->event_loop, wl_display_get_fd(backend->wl_display), WL_EVENT_READABLE,
        wroc_listen_backend_display_read, backend);

    wroc_backend_output_create(backend);

    wl_display_flush(backend->wl_display);
}

void wroc_backend_destroy(wroc_backend* backend)
{
    wl_event_source_remove(backend->event_source);

    if (backend->keyboard) backend->keyboard = nullptr;
    if (backend->pointer)  backend->pointer = nullptr;

    backend->outputs.clear();

    zxdg_decoration_manager_v1_destroy(backend->decoration_manager);
    wl_compositor_destroy(backend->wl_compositor);
    xdg_wm_base_destroy(backend->xdg_wm_base);
    wl_seat_destroy(backend->seat);

    wl_registry_destroy(backend->wl_registry);

    wl_display_disconnect(backend->wl_display);

    wrei_get_registry(backend)->destroy(backend, backend->wrei.version);
}
