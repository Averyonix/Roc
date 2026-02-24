#include "internal.hpp"

CORE_OBJECT_EXPLICIT_DEFINE(scene_context);
CORE_OBJECT_EXPLICIT_DEFINE(scene_window);

struct event_handler
{
    scene_context* ctx;
    void operator()(io_event* event) const {
        switch (event->type) {
            break;case io_event_type::shutdown_requested: io_stop(event->ctx);
            break;case io_event_type::input_added:        scene_handle_input_added(  ctx, event->input.device);
            break;case io_event_type::input_removed:      scene_handle_input_removed(ctx, event->input.device);
            break;case io_event_type::input_event:        scene_handle_input(        ctx, event->input);
            break;case io_event_type::output_configure:   io_output_request_frame(event->output.output, ctx->render.usage);
            break;case io_event_type::output_redraw:      scene_render(ctx, event->output.output, event->output.target);
            break;case io_event_type::output_added:
                  case io_event_type::output_removed:
                log_warn("io::{}", core_enum_to_string(event->type));
        }
    }
};

auto scene_create(gpu_context* gpu, io_context* io) -> ref<scene_context>
{
    auto scene = core_create<scene_context>();

    scene->gpu = gpu;

    scene->io = io;
    io_set_event_handler(io, event_handler{scene.get()});

    scene->root_tree = scene_tree_create(scene.get());

    for (auto layer : magic_enum::enum_values<scene_layer>()) {
        auto* tree = (scene->layers[layer] = scene_tree_create(scene.get())).get();
        scene_node_set_transform(tree, scene->root_transform.get());
        scene_tree_place_above(scene->root_tree.get(), nullptr, tree);
    }

    scene->root_transform = scene_transform_create(scene.get());

    scene_render_init(scene.get());

    scene->keyboard = scene_keyboard_create(scene.get());
    scene->pointer = scene_pointer_create(scene.get());

    return scene;
}

auto scene_get_layer(scene_context* ctx, scene_layer layer) -> scene_tree*
{
    return ctx->layers[layer].get();
}
auto scene_get_root_transform(scene_context* ctx) -> scene_transform*
{
    return ctx->root_transform.get();
}

void scene_broadcast_event(scene_context* ctx, scene_event* event)
{
    for (auto* client : ctx->clients) {
        scene_client_post_event(client, event);
    }
}
