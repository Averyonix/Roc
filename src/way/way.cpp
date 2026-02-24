#include "internal.hpp"

CORE_OBJECT_EXPLICIT_DEFINE(way_server);

way_server::~way_server()
{
    wl_event_loop_fd = nullptr;
    wl_display_terminate(wl_display);
    wl_display_destroy(wl_display);
}

auto way_create(core_event_loop* event_loop, gpu_context* gpu, scene_context* scene) -> ref<way_server>
{
    auto server = core_create<way_server>();

    server->event_loop = event_loop;
    server->gpu = gpu;
    server->scene = scene;

    server->wl_display = wl_display_create();

    wl_display_set_default_max_buffer_size(server->wl_display, 4096);

    server->socket_name = wl_display_add_socket_auto(server->wl_display);

    server->wl_event_loop_fd = core_fd_reference(wl_event_loop_get_fd(wl_display_get_event_loop(server->wl_display)));
    core_fd_set_listener(server->wl_event_loop_fd.get(), event_loop, core_fd_event_bit::readable,
        [server = server.get()](core_fd* fd, core_fd_event_bits events) {
            log_error("wl_display - read started");
            unix_check(wl_event_loop_dispatch(wl_display_get_event_loop(server->wl_display), 0));
            wl_display_flush_clients(server->wl_display);
            log_error("wl_display - read finished");
        });

    way_global(server.get(), wl_shm);
    way_global(server.get(), wl_compositor);
    way_global(server.get(), xdg_wm_base);

    server->sampler = gpu_sampler_create(gpu, VK_FILTER_NEAREST, VK_FILTER_LINEAR);

    server->client = scene_client_create(scene);
    scene_client_set_event_handler(server->client.get(), [client = server->client.get()](scene_event* event) {
        switch (event->type) {
            break;case scene_event_type::keyboard_key:
            break;case scene_event_type::keyboard_modifier:
            break;case scene_event_type::pointer_motion:
            break;case scene_event_type::pointer_button:
            break;case scene_event_type::pointer_scroll:
            break;case scene_event_type::focus_pointer:
            break;case scene_event_type::focus_keyboard:
            break;case scene_event_type::window_resize:
                log_error("TODO: window resize");
                // scene_texture_set_dst(canvas, {{}, event->window.resize, core_xywh});
                // scene_window_set_size(event->window.window, event->window.resize);
        }
    });

    return server;
}

wl_global* way_global_(way_server* server, const wl_interface* interface, i32 version, wl_global_bind_func_t bind, void* data)
{
    core_assert(version <= interface->version);
    return wl_global_create(server->wl_display, interface, version, data ?: server, bind);
}

wl_resource* way_resource_create_(wl_client* client, const wl_interface* interface, int version, int id, const void* impl, core_object* data, bool refcount)
{
    auto resource = wl_resource_create(client, interface, version, id);
    if (refcount) {
        core_add_ref(data);
        wl_resource_set_implementation(resource, impl, data, [](wl_resource* resource) {
            core_remove_ref(static_cast<core_object*>(wl_resource_get_user_data(resource)));
        });
    } else {
        wl_resource_set_implementation(resource, impl, data, nullptr);
    }
    return resource;
}

u32 way_next_serial(way_server* server)
{
    return wl_display_next_serial(server->wl_display);
}

void way_queue_client_flush(way_server* server)
{
    log_trace("way_queue_client_flush");
}

void way_simple_destroy(wl_client* client, wl_resource* resource)
{
    wl_resource_destroy(resource);
}
