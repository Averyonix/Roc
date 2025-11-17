#include "server.hpp"

u32 wroc_get_elapsed_milliseconds(wroc_server* server)
{
    // TODO: This will elapse after 46 days of runtime, should we base it on surface epoch?

    auto now = std::chrono::steady_clock::now();
    auto elapsed = now - server->epoch;
    return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
}

void wroc_run(int argc, char* argv[])
{
    wrei_ref server = wrei_adopt_ref(new wroc_server {});
    log_warn("server = {}", (void*)server.get());

    server->seat = wrei_adopt_ref(new wroc_seat{});
    server->seat->name = "seat-0";

    server->epoch = std::chrono::steady_clock::now();

    if (getenv("WROC_WAYLAND_DEBUG_SERVER")) {
        setenv("WAYLAND_DEBUG", "1", true);
    } else {
        unsetenv("WAYLAND_DEBUG");
    }
    server->display = wl_display_create();
    unsetenv("WAYLAND_DEBUG");
    server->event_loop = wl_display_get_event_loop(server->display);

    wroc_backend_init(server.get());
    wroc_renderer_create(server.get());

    const char* socket = wl_display_add_socket_auto(server->display);

    wl_global_create(server->display, &wl_shm_interface,           wl_shm_interface.version,           server.get(),       wroc_wl_shm_bind_global);
    wl_global_create(server->display, &wl_compositor_interface,    wl_compositor_interface.version,    server.get(),       wroc_wl_compositor_bind_global);
    wl_global_create(server->display, &wl_subcompositor_interface, wl_subcompositor_interface.version, server.get(),       wroc_wl_subcompositor_bind_global);
    wl_global_create(server->display, &xdg_wm_base_interface,      xdg_wm_base_interface.version,      server.get(),       wroc_xdg_wm_base_bind_global);
    wl_global_create(server->display, &wl_seat_interface,          wl_seat_interface.version,          server->seat.get(), wroc_wl_seat_bind_global);

    wl_global_create(server->display, &zwp_linux_dmabuf_v1_interface, 3/* zwp_linux_dmabuf_v1_interface.version */, server.get(), wroc_zwp_linux_dmabuf_v1_bind_global);

    log_info("Running compositor on: {}", socket);

    wl_display_run(server->display);

    log_info("Compositor shutting down");

    if (server->backend) {
        wroc_backend_destroy(server->backend);
    }

    wl_display_destroy_clients(server->display);

    server->renderer = nullptr;

    wl_display_destroy(server->display);

    log_info("Display destroyed");

    server = nullptr;

    log_info("Shutdown complete");
}

void wroc_terminate(wroc_server* server)
{
    wl_display_terminate(server->display);
}
