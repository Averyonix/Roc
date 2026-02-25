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

void scene_window_request_reframe(scene_window* window, rect2f32 frame)
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

// -----------------------------------------------------------------------------

auto scene_find_window_at(scene_context* ctx, vec2f32 point) -> scene_window*
{
    // TOOD: It is relatively expensive to build this window-tree map.
    //       If we start using this function in any hot paths, we will want to
    //       cache this or find a better way to map from scene nodes to windows.

    ankerl::unordered_dense::map<scene_tree*, scene_window*> window_trees;
    for (auto* window : ctx->windows) {
        window_trees.insert({window->tree.get(), window});
    }

    // TODO: This will ignore any `input_plane`s currently.
    //       Should we provide (optional) mappings from `input_plane` back to windows
    //       and then intersect against both `input_plane`s and `window` frames?

    return [&](this auto&& self, scene_tree* parent) -> scene_window* {
        for (auto* child : parent->children | std::views::reverse) {
            if (child->type != scene_node_type::tree) continue;
            auto tree = static_cast<scene_tree*>(child);

            // Iterate children first in case there is a child window in front
            if (auto* window = self(tree)) {
                return window;
            }

            // Check if this is a window's root tree
            if (auto w = window_trees.find(tree); w != window_trees.end()) {
                if (core_rect_contains(scene_window_get_frame(w->second), point)) {
                    return w->second;
                }
            }
        }

        return nullptr;
    }(ctx->root_tree.get());
}
