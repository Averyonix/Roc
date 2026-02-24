#include "internal.hpp"

CORE_OBJECT_EXPLICIT_DEFINE(scene_client);

scene_client::~scene_client()
{
    // All client windows must be destroyed before the client
    for (auto* window : ctx->windows) {
        core_assert(window->client != this);
    }

    if (ctx->keyboard->focus.client == this) {
        scene_keyboard_ungrab(this);
    }

    std::erase(ctx->clients, this);
}

auto scene_client_create(scene_context* ctx) -> ref<scene_client>
{
    auto client = core_create<scene_client>();
    client->ctx = ctx;
    ctx->clients.emplace_back(client.get());
    return client;
}

void scene_client_set_event_handler(scene_client* client, std::move_only_function<scene_event_handler_fn>&& event_handler)
{
    client->event_handler = std::move(event_handler);
}

void scene_client_post_event(scene_client* client, scene_event* event)
{
    client->event_handler(event);
}
