#include "internal.hpp"

static
auto find_surface(way_client* client, scene_window* window) -> way_surface*
{
    for (auto* surface : client->surfaces) {
        if (surface->toplevel.window.get() == window) {
            return surface;
        }
    }

    return nullptr;
}

static
void handle_event(way_client* client, scene_event* event)
{
    switch (event->type) {
        break;case scene_event_type::window_reposition: {
            auto* surface = find_surface(client, event->window.window);
            way_toplevel_on_reposition(surface, event->window.reposition.frame, event->window.reposition.gravity);
        }
        break;case scene_event_type::redraw: {
            for (auto* surface : client->surfaces) {
                way_surface_on_redraw(surface);
            }
        }
        break;default:
            ;
    }
}

// -----------------------------------------------------------------------------

static
void on_destroy(wl_listener* listener, void* data) {
    auto* client = way_get_userdata<way_client>(listener);
    client->server->client.map.erase(client->wl_client);
}

void way_on_client_create(wl_listener* listener, void* data)
{
    auto* server = way_get_userdata<way_server>(listener);
    auto* wl_client = static_cast<struct wl_client*>(data);

    auto client = core_create<way_client>();
    client->server = server;

    client->scene = scene_client_create(server->scene);
    scene_client_set_event_handler(client->scene.get(), [client = client.get()](scene_event* event) {
        handle_event(client, event);
    });

    server->client.map.insert({wl_client, client});

    client->on_destroy.data = client.get();
    client->on_destroy.listener.notify = on_destroy;
    wl_client_add_destroy_listener(wl_client, &client->on_destroy.listener);
}

way_client* way_client_from(way_server* server, wl_client* client)
{
    auto iter = server->client.map.find(client);
    return iter == server->client.map.end() ? nullptr : iter->second.get();
}
