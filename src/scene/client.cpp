#include "internal.hpp"

WREI_OBJECT_EXPLICIT_DEFINE(wrui_client);

wrui_client::~wrui_client()
{
    // All client windows must be destroyed before the client
    for (auto* window : ctx->windows) {
        wrei_assert(window->client != this);
    }

    if (ctx->keyboard->focus.client == this) {
        wrui_keyboard_ungrab(this);
    }

    std::erase(ctx->clients, this);
}

auto wrui_client_create(wrui_context* ctx) -> ref<wrui_client>
{
    auto client = wrei_create<wrui_client>();
    client->ctx = ctx;
    ctx->clients.emplace_back(client.get());
    return client;
}

void wrui_client_set_event_handler(wrui_client* client, std::move_only_function<wrui_event_handler_fn>&& event_handler)
{
    client->event_handler = std::move(event_handler);
}

void wrui_client_post_event(wrui_client* client, wrui_event* event)
{
    client->event_handler(event);
}
