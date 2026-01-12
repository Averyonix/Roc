#include "wroc.hpp"

u32 wroc_get_elapsed_milliseconds()
{
    // TODO: This will elapse after 46 days of runtime, should we base it on surface epoch?

    auto now = std::chrono::steady_clock::now();
    auto elapsed = now - server->epoch;
    return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
}

wl_global* wroc_global(const wl_interface* interface, i32 version, wl_global_bind_func_t bind, void* data)
{
    assert(version <= interface->version);
    return wl_global_create(server->display, interface, version, data, bind);
}

void wroc_queue_client_flush()
{
    if (server->client_flushes_pending) return;
    server->client_flushes_pending++;
    wrei_event_loop_enqueue(server->event_loop.get(), [] {
        if (server->client_flushes_pending) {
            wl_display_flush_clients(server->display);
            server->client_flushes_pending = 0;
        }
    });
}

static
void signal_handler(int sig)
{
    if (sig == SIGINT) {
        // Immediately unregister SIGINT in case of unresponsive event loop
        std::signal(sig, SIG_DFL);
    }

    // TODO: Dedicated stop eventfd for signal safe handling
    wrei_event_loop_enqueue(server->event_loop.get(), [sig] {
        const char* name = "Unknown";
        switch (sig) {
            break;case SIGTERM: name = "Terminate";
            break;case SIGINT:  name = "Interrupt";
        }

        log_error("{} ({}) signal receieved", name, sig);

        if (sig == SIGTERM || sig == SIGINT) {
            wroc_terminate();
        }
    });
}

void wroc_terminate()
{
    // TODO: Proper termination will require further event handling (e.g. waiting for GPU jobs to complete)
    //       Send event to start termination, then close event loop only after all subsystems have closed.
    wrei_event_loop_stop(server->event_loop.get());
}

wroc_server* server;

void wroc_run(int argc, char* argv[])
{
    wrei_log_set_history_enabled(true);

    wroc_render_options render_options = {};
    wroc_backend_type backend_type = getenv("WAYLAND_DISPLAY")
        ? wroc_backend_type::wayland
        : wroc_backend_type::direct;

    bool show_imgui_on_startup = false;

    std::optional<std::string> x11_socket;

    std::optional<std::string> log_file;

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
        } else if (arg == "--log-file") {
            if (i + 1 >= argc || argv[i + 1][0] == '\0') {
                log_error("Expected valid path after --log-file");
                return;
            }
            log_file = argv[++i];
        } else {
            log_error("Unrecognized flag: {}", arg);
            return;
        }
    }

    wrei_init_log(wrei_log_level::trace, log_file ? log_file->c_str() : nullptr);

    auto server_ref = wrei_create<wroc_server>();
    server = server_ref.get();
    auto event_loop = wrei_event_loop_create();
    server->event_loop = event_loop;
    log_warn("Server = {}", (void*)server);

    if (backend_type == wroc_backend_type::direct) {
        server->main_mod = wroc_modifiers::super;
        server->main_mod_evdev = KEY_LEFTMETA;
    } else {
        server->main_mod = wroc_modifiers::alt;
        server->main_mod_evdev = KEY_LEFTALT;
    }

    // Seat

    wroc_seat_init();

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
    auto display_event_source = wrei_event_loop_add_fd(event_loop.get(), wl_event_loop_get_fd(wl_event_loop), EPOLLIN,
        [&](int fd, u32 events) {
            server->client_flushes_pending++;
            wrei_unix_check_n1(wl_event_loop_dispatch(wl_event_loop, 0));
            wl_display_flush_clients(server->display);
            server->client_flushes_pending--;
        });

    // Output layout

    wroc_output_layout_init();

    // Renderer

    log_warn("Initializing renderer");
    wroc_renderer_create(render_options);

    // Cursor

    wroc_cursor_create();

    // ImGui

    log_warn("Initializing imgui");
    wroc_imgui_init();
    wroc_debug_gui_init(show_imgui_on_startup);

    // Backend

    log_warn("Initializing backend");
    wroc_backend_init(backend_type);
    log_warn("Backend initialized");

    // Register globals

    WROC_GLOBAL(wl_shm);
    if (!(render_options >= wroc_render_options::no_dmabuf)) {
        WROC_GLOBAL(zwp_linux_dmabuf_v1);
    }
    WROC_GLOBAL(wl_compositor);
    WROC_GLOBAL(wl_subcompositor);
    WROC_GLOBAL(wl_data_device_manager);
    WROC_GLOBAL(xdg_wm_base);
    WROC_GLOBAL(wl_seat, server->seat.get());
    WROC_GLOBAL(zwp_pointer_gestures_v1);
    WROC_GLOBAL(wp_viewporter);
    WROC_GLOBAL(zwp_relative_pointer_manager_v1);
    WROC_GLOBAL(zwp_pointer_constraints_v1);
    WROC_GLOBAL(zxdg_decoration_manager_v1);

    // Run

    log_warn("WAYLAND_DISPLAY={}", server->socket);
    if (backend_type == wroc_backend_type::direct) {
        setenv("XDG_CURRENT_DESKTOP", PROGRAM_NAME, true);
    }
    if (x11_socket) {
        wroc_spawn("xwayland-satellite", {"xwayland-satellite", x11_socket->c_str()}, {});
        server->x11_socket = *x11_socket;
    }

    log_info("Running compositor on: {}", server->socket);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    wrei_event_loop_run(event_loop.get());

    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_IGN);

    // Shutdown

    log_info("Compositor shutting down");

    // Keep Wren alive until all other resources have been destroyed safely
    log_info("Flushing wren submissions");
    ref wren = server->renderer->wren;
    wren_wait_idle(wren.get());

    log_info("Destroying: backend");
    server->backend = nullptr;

    log_info("Destroying: clients");
    wl_display_destroy_clients(server->display);

    log_info("Destroying: renderer");
    server->renderer = nullptr;

    log_info("Destroying: wl_display");
    wl_display_destroy(server->display);
    display_event_source->mark_defunct();
    display_event_source = nullptr;

    log_info("Destroying: server");
    server_ref = nullptr;

    log_info("Destroying: wren");
    wren = nullptr;

    log_info("Destroying: event loop");
    event_loop = nullptr;

    log_info("Shutdown complete");
}
