#include "internal.hpp"

scene_window::~scene_window()
{
    scene_window_unmap(this);
    std::erase(client->ctx->windows, this);
}

auto scene_window_create(scene_client* client) -> ref<scene_window>
{
    auto window = core_create<scene_window>();
    window->client = client;

    auto* ctx = client->ctx;

    ctx->windows.emplace_back(window.get());

    window->transform = scene_transform_create(ctx);
    scene_node_set_transform(window->transform.get(), ctx->root_transform.get());

    window->tree = scene_tree_create(ctx);
    scene_tree_place_above(scene_get_layer(ctx, scene_layer::window), nullptr, window->tree.get());

    return window;
}

auto scene_window_get_tree(scene_window* window) -> scene_tree*
{
    return window->tree.get();
}

auto scene_window_get_transform(scene_window* window) -> scene_transform*
{
    return window->transform.get();
}

void scene_window_set_title(scene_window* window, std::string_view title)
{
    window->title = title;
}

void scene_window_request_frame(scene_window* window, rect2f32 frame)
{
    scene_client_post_event(window->client, ptr_to(scene_event {
        .type = scene_event_type::window_reframe,
        .window = {
            .window = window,
            .reframe = frame,
        }
    }));
}

void scene_window_set_frame(scene_window* window, rect2f32 frame)
{
    window->extent = frame.extent;
    scene_transform_update(window->transform.get(), frame.origin, 1.f);
}

auto scene_window_get_frame(scene_window* window) -> rect2f32
{
    return {
        scene_transform_get_global(window->transform.get()).translation,
        window->extent,
        core_xywh
    };
}

void scene_window_map(scene_window* window)
{
    if (window->mapped) return;

    scene_tree_place_above(scene_get_layer(window->client->ctx, scene_layer::window), nullptr, window->tree.get());

    window->mapped = true;
}

void scene_window_raise(scene_window* window)
{
    if (!window->mapped) return;

    scene_tree_place_above(scene_get_layer(window->client->ctx, scene_layer::window), nullptr, window->tree.get());
}

void scene_window_unmap(scene_window* window)
{
    if (!window->mapped) return;

    scene_node_unparent(window->tree.get());

    window->mapped = false;
}
