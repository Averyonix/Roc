#include "server.hpp"

u32 wroc_get_elapsed_milliseconds(wroc_server* server)
{
    // TODO: This will elapse after 46 days of runtime, should we base it on surface epoch?

    auto now = std::chrono::steady_clock::now();
    auto elapsed = now - server->epoch;
    return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
}

wl_global* wroc_server_global(wroc_server* server, const wl_interface* interface, i32 version, wl_global_bind_func_t bind, void* data)
{
    assert(version <= interface->version);
    return wl_global_create(server->display, interface, version, data ?: server, bind);
}

void wroc_run(int argc, char* argv[])
{
    wrei_log_set_history_enabled(true);

    wroc_render_options render_options = {};
    wroc_backend_type backend_type = getenv("WAYLAND_DISPLAY")
        ? wroc_backend_type::wayland
        : wroc_backend_type::direct;

    bool show_imgui_on_startup = false;

    std::optional<std::string> x11_socket;

    for (int i = 1; i < argc; ++i) {
        auto arg = std::string_view(argv[i]);
        if (arg == "--no-dmabuf") {
            render_options |= wroc_render_options::no_dmabuf;
        } else if (arg == "--separate-draws") {
            render_options |= wroc_render_options::separate_draws;
        } else if (arg == "--imgui") {
            show_imgui_on_startup = true;
        } else if (arg == "--xwayland") {
            if (i + 1 >= argc || argv[i + 1][0] != ':') {
                log_error("Expected x11 socket path after --xwayland");
                return;
            }
            x11_socket = argv[++i];
        } else {
            log_error("Unrecognized flag: {}", arg);
            return;
        }
    }

    ref<wroc_server> server_ref = wrei_create<wroc_server>();
    wroc_server* server = server_ref.get();
    server->event_loop = wrei_event_loop_create();
    log_warn("server = {}", (void*)server);

    if (backend_type == wroc_backend_type::direct) {
        server->main_mod = wroc_modifiers::super;
        server->main_mod_evdev = KEY_LEFTMETA;
    } else {
        server->main_mod = wroc_modifiers::alt;
        server->main_mod_evdev = KEY_LEFTALT;
    }

    // Seat

    wroc_seat_init(server);

    server->epoch = std::chrono::steady_clock::now();

    // Init libwayland

    if ((getenv("WROC_WAYLAND_DEBUG_SERVER")?:"")[0]!=0) {
        setenv("WAYLAND_DEBUG", "1", true);
    } else {
        unsetenv("WAYLAND_DEBUG");
    }
    server->display = wl_display_create();
    unsetenv("WAYLAND_DEBUG");

    wl_display_set_default_max_buffer_size(server->display, 4096);

    server->socket = wl_display_add_socket_auto(server->display);

    auto wl_event_loop = wl_display_get_event_loop(server->display);
    auto _ = wrei_event_loop_add_fd(server->event_loop.get(), wl_event_loop_get_fd(wl_event_loop), EPOLLIN,
        [&](int fd, u32 events) {
            wrei_unix_check_n1(wl_event_loop_dispatch(wl_event_loop, 0));
        });

    server->event_loop->prepolls.emplace_back([&] {
        wl_display_flush_clients(server->display);
    });

    // Output layout

    wroc_output_layout_init(server);

    // Renderer

    log_warn("Initializing renderer");
    wroc_renderer_create(server, render_options);

    // Cursor

    wroc_cursor_create(server);

    // ImGui

    log_warn("Initializing imgui");
    wroc_imgui_init(server);
    wroc_debug_gui_init(server, show_imgui_on_startup);

    // Backend

    log_warn("Initializing backend");
    wroc_backend_init(server, backend_type);
    log_warn("Backend initialized");

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
    WROC_SERVER_GLOBAL(server, wp_viewporter);
    WROC_SERVER_GLOBAL(server, zwp_relative_pointer_manager_v1);
    WROC_SERVER_GLOBAL(server, zwp_pointer_constraints_v1);

    // Run

    log_warn("WAYLAND_DISPLAY={}", server->socket);
    if (backend_type == wroc_backend_type::direct) {
        setenv("XDG_CURRENT_DESKTOP", PROGRAM_NAME, true);
    }
    if (x11_socket) {
        wroc_server_spawn(server, "xwayland-satellite", {"xwayland-satellite", x11_socket->c_str()}, {});
        server->x11_socket = *x11_socket;
    }

    log_info("Running compositor on: {}", server->socket);

    wrei_event_loop_run(server->event_loop.get());

    // Shutdown

    log_info("Compositor shutting down");

    server->backend = nullptr;

    wl_display_destroy_clients(server->display);

    server->renderer = nullptr;

    wl_display_destroy(server->display);

    log_info("Display destroyed");

    server_ref = nullptr;

    log_info("Shutdown complete");
}

void wroc_terminate(wroc_server* server)
{
    wrei_event_loop_stop(server->event_loop.get());
}
