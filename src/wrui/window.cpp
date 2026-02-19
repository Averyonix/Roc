#include "internal.hpp"

wrui_window::~wrui_window()
{
    std::erase(client->ctx->windows, this);
}

auto wrui_window_create(wrui_client* client) -> ref<wrui_window>
{
    auto window = wrei_create<wrui_window>();
    window->client = client;

    auto* ctx = client->ctx;

    ctx->windows.emplace_back(window.get());

    window->transform = wrui_transform_create(ctx);
    wrui_node_set_transform(window->transform.get(), ctx->root_transform.get());

    window->tree = wrui_tree_create(ctx);
    wrui_tree_place_above(ctx->scene.get(), nullptr, window->tree.get());

    return window;
}

auto wrui_window_get_tree(wrui_window* window) -> wrui_tree*
{
    return window->tree.get();
}

auto wrui_window_get_transform(wrui_window* window) -> wrui_transform*
{
    return window->transform.get();
}

void wrui_window_set_size(wrui_window* window, vec2u32 size)
{
    window->size = size;
}

void wrui_window_map(wrui_window* window)
{
    if (window->mapped) return;

    wrui_tree_place_above(window->client->ctx->scene.get(), nullptr, window->tree.get());

    window->mapped = true;
}

void wrui_window_unmap(wrui_window* window)
{
    if (!window->mapped) return;

    wrui_node_unparent(window->tree.get());

    window->mapped = false;
}
