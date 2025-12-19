#include "server.hpp"

u32 wroc_get_elapsed_milliseconds(wroc_server* server)
{
    // TODO: This will elapse after 46 days of runtime, should we base it on surface epoch?

    auto now = std::chrono::steady_clock::now();
    auto elapsed = now - server->epoch;
    return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
}

wl_global* wroc_server_global(auto* server, const wl_interface* interface, i32 version, wl_global_bind_func_t bind, void* data)
{
    assert(version <= interface->version);
    return wl_global_create(server->display, interface, version, data ?: server, bind);
}

void wroc_run(int argc, char* argv[])
{
    wroc_render_options render_options = {};
    wroc_backend_type backend_type = wroc_backend_type::wayland;
    wroc_options options = wroc_options::none;
    for (int i = 1; i < argc; ++i) {
        auto arg = std::string_view(argv[i]);
        if (arg == "--no-dmabuf") {
            render_options |= wroc_render_options::no_dmabuf;
        } else if (arg == "--separate-draws") {
            render_options |= wroc_render_options::separate_draws;
        } else if (arg == "--direct") {
            // TODO: Auto detect backend
            backend_type = wroc_backend_type::direct;
        } else if (arg == "--imgui") {
            options |= wroc_options::imgui;
            wrei_log_set_history_enabled(true);
        } else {
            log_error("Unrecognized flag: {}", arg);
            return;
        }
    }

    ref<wroc_server> server_ref = wrei_create<wroc_server>();
    wroc_server* server = server_ref.get();
    log_warn("server = {}", (void*)server);

    server->options = options;

    // Seat

    server->seat = wrei_create<wroc_seat>();
    server->seat->name = "seat-0";

    server->epoch = std::chrono::steady_clock::now();

    // Init libwayland

    if (getenv("WROC_WAYLAND_DEBUG_SERVER")) {
        setenv("WAYLAND_DEBUG", "1", true);
    } else {
        unsetenv("WAYLAND_DEBUG");
    }
    server->display = wl_display_create();
    unsetenv("WAYLAND_DEBUG");
    server->event_loop = wl_display_get_event_loop(server->display);

    wl_display_set_default_max_buffer_size(server->display, 65'536);

    const char* socket = wl_display_add_socket_auto(server->display);

    // Renderer

    log_warn("Initializing renderer");

    wroc_renderer_create(server, render_options);

    // ImGui

    log_warn("Initializing imgui");

    if (server->options >= wroc_options::imgui) {
        wroc_imgui_init(server);
    }

    // Backend

    log_warn("Initializing backend");

    wroc_backend_init(server, backend_type);

    log_warn("Backend initialized");

    // Cursor

    wroc_cursor_create(server);

    // Register globals

    WROC_SERVER_GLOBAL(server, wl_shm);
    if (!(render_options >= wroc_render_options::no_dmabuf)) {
        WROC_SERVER_GLOBAL(server, zwp_linux_dmabuf_v1);
    }
    WROC_SERVER_GLOBAL(server, wl_compositor);
    WROC_SERVER_GLOBAL(server, wl_subcompositor);
    WROC_SERVER_GLOBAL(server, wl_data_device_manager);
    WROC_SERVER_GLOBAL(server, xdg_wm_base);
    WROC_SERVER_GLOBAL(server, wl_seat, server->seat.get());
    WROC_SERVER_GLOBAL(server, zwp_pointer_gestures_v1);

    // Run

    log_warn("Setting WAYLAND_DISPLAY={}", socket);
    setenv("WAYLAND_DISPLAY", socket, true);
    setenv("XDG_CURRENT_DESKTOP", PROGRAM_NAME, true);
    unsetenv("DISPLAY");

    log_info("Running compositor on: {}", socket);

    wl_display_run(server->display);

    // Shutdown

    log_info("Compositor shutting down");

    if (server->backend) {
        server->backend = nullptr;
    }

    wl_display_destroy_clients(server->display);

    server->renderer = nullptr;

    wl_display_destroy(server->display);

    log_info("Display destroyed");

    server_ref = nullptr;

    log_info("Shutdown complete");
}

void wroc_terminate(wroc_server* server)
{
    wl_display_terminate(server->display);
}
