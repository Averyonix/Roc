#include "backend.hpp"

std::span<const char* const> backend_get_required_instance_extensions(Backend*)
{
    static constexpr std::array extensions {
        VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
    };

    return extensions;
}

// -----------------------------------------------------------------------------

static
void listen_xdg_wm_base_ping(void*, struct xdg_wm_base* xdg_wm_base, u32 serial)
{
    log_trace("xdg_wm_base::ping(serial = {})", serial);

    xdg_wm_base_pong(xdg_wm_base, serial);
}

const xdg_wm_base_listener listeners::xdg_wm_base = {
    .ping = listen_xdg_wm_base_ping,
};

// -----------------------------------------------------------------------------

void listen_registry_global(void *data, wl_registry*, u32 name, const char* interface, u32 version)
{
    auto* backend = static_cast<Backend*>(data);

    bool matched;

#define IF_BIND_INTERFACE(Interface, Member, ...) \
    matched = strcmp(interface, Interface.name) == 0; \
    if (matched) { \
        u32 bind_version = std::min(version, u32(Interface.version)); \
        backend->Member = static_cast<decltype(backend->Member)>(wl_registry_bind(backend->wl_registry, name, &Interface, bind_version)); \
        log_info("wl_registry::global(name = {:2}, interface = {:41}, version = {:2} ({}))", name, interface, version, bind_version); \
        { __VA_ARGS__ }\
        break; \
    }

    do {
        IF_BIND_INTERFACE(wl_compositor_interface, wl_compositor)
        IF_BIND_INTERFACE(xdg_wm_base_interface, xdg_wm_base, {
            xdg_wm_base_add_listener(backend->xdg_wm_base, &listeners::xdg_wm_base, backend);
        })
        IF_BIND_INTERFACE(zxdg_decoration_manager_v1_interface, decoration_manager)
        IF_BIND_INTERFACE(wl_seat_interface, seat, {
            wl_seat_add_listener(backend->seat, &listeners::wl_seat, backend);
        })

        log_trace("wl_registry::global(name = {:2}, interface = {:41}, version = {:2})", name, interface, version);
    } while (false);

#undef IF_BIND_INTERFACE
}

void listen_registry_global_remove(void* /* data */, wl_registry*, u32 name)
{
    log_warn("wl_registry::global_remove(name = {:2})", name);
}

const wl_registry_listener listeners::wl_registry {
    .global        = listen_registry_global,
    .global_remove = listen_registry_global_remove,
};

int listen_backend_display_read(int fd, u32 mask, void* data)
{
    auto* backend = static_cast<Backend*>(data);

    // log_trace("backend display read, events = {:#x}", events);

    timespec timeout = {};
    int res = wl_display_dispatch_timeout(backend->wl_display, &timeout);
    if (res < 0) {
        log_error("  wl_display_dispatch: {}", res);
        wl_event_source_remove(backend->event_source);
    }

    wl_display_flush(backend->wl_display);

    return 1;
}

void backend_init(Server* server)
{
    auto* backend = new Backend{};
    backend->server = server;

    backend->wl_display = wl_display_connect(nullptr);
    backend->wl_registry = wl_display_get_registry(backend->wl_display);

    wl_registry_add_listener(backend->wl_registry, &listeners::wl_registry, backend);
    wl_display_roundtrip(backend->wl_display);

    server->backend = backend;

    backend->event_source = wl_event_loop_add_fd(server->event_loop, wl_display_get_fd(backend->wl_display), WL_EVENT_READABLE,
        listen_backend_display_read, backend);

    backend_output_create(backend);

    wl_display_flush(backend->wl_display);
}

void backend_destroy(Backend* backend)
{
    delete backend;
}
