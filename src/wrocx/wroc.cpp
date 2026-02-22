#include "internal.hpp"

WROC_NAMESPACE_USE

WREI_OBJECT_EXPLICIT_DEFINE(wroc_server);

WROC_NAMESPACE_BEGIN

wroc_server::~wroc_server()
{
    wl_event_loop_fd = nullptr;
    wl_display_terminate(wl_display);
    wl_display_destroy(wl_display);
}

auto wroc_create(wrei_event_loop* event_loop, wren_context* wren, wrui_context* wrui) -> ref<wroc_server>
{
    auto server = wrei_create<wroc_server>();

    server->event_loop = event_loop;
    server->wren = wren;
    server->wrui = wrui;

    server->wl_display = wl_display_create();

    wl_display_set_default_max_buffer_size(server->wl_display, 4096);

    server->socket_name = wl_display_add_socket_auto(server->wl_display);

    server->wl_event_loop_fd = wrei_fd_reference(wl_event_loop_get_fd(wl_display_get_event_loop(server->wl_display)));
    wrei_fd_set_listener(server->wl_event_loop_fd.get(), event_loop, wrei_fd_event_bit::readable,
        [server = server.get()](wrei_fd* fd, wrei_fd_event_bits events) {
            log_error("wl_display - read started");
            unix_check(wl_event_loop_dispatch(wl_display_get_event_loop(server->wl_display), 0));
            wl_display_flush_clients(server->wl_display);
            log_error("wl_display - read finished");
        });

    wroc_global(server.get(), wl_shm);
    wroc_global(server.get(), wl_compositor);
    wroc_global(server.get(), xdg_wm_base);

    server->sampler = wren_sampler_create(wren, VK_FILTER_NEAREST, VK_FILTER_LINEAR);

    server->client = wrui_client_create(wrui);
    wrui_imgui_request_frame(server->client.get());
    wrui_client_set_event_handler(server->client.get(), [client = server->client.get()](wrui_event* event) {
        switch (event->type) {
            break;case wrui_event_type::imgui_frame:
                ImGui::ShowDemoWindow();
            break;case wrui_event_type::window_resize:
                log_error("TODO: window resize");
                // wrui_texture_set_dst(canvas, {{}, event->window.resize, wrei_xywh});
                // wrui_window_set_size(event->window.window, event->window.resize);
        }
    });

    return server;
}

wl_global* wroc_global_(wroc_server* server, const wl_interface* interface, i32 version, wl_global_bind_func_t bind, void* data)
{
    wrei_assert(version <= interface->version);
    return wl_global_create(server->wl_display, interface, version, data ?: server, bind);
}

wl_resource* wroc_resource_create_(wl_client* client, const wl_interface* interface, int version, int id, const void* impl, wrei_object* data, bool refcount)
{
    auto resource = wl_resource_create(client, interface, version, id);
    if (refcount) {
        wrei_add_ref(data);
        wl_resource_set_implementation(resource, impl, data, [](wl_resource* resource) {
            wrei_remove_ref(static_cast<wrei_object*>(wl_resource_get_user_data(resource)));
        });
    } else {
        wl_resource_set_implementation(resource, impl, data, nullptr);
    }
    return resource;
}

u32 wroc_next_serial(wroc_server* server)
{
    return wl_display_next_serial(server->wl_display);
}

void wroc_queue_client_flush(wroc_server* server)
{
    log_trace("wroc_queue_client_flush");
}

void wroc_simple_destroy(wl_client* client, wl_resource* resource)
{
    wl_resource_destroy(resource);
}

WROC_NAMESPACE_END
