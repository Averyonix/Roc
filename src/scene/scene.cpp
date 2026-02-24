#include "internal.hpp"

WREI_OBJECT_EXPLICIT_DEFINE(wrui_context);
WREI_OBJECT_EXPLICIT_DEFINE(wrui_window);

struct event_handler
{
    wrui_context* ctx;
    void operator()(wrio_event* event) const {
        switch (event->type) {
            break;case wrio_event_type::shutdown_requested: wrio_stop(event->ctx);
            break;case wrio_event_type::input_added:        wrui_handle_input_added(  ctx, event->input.device);
            break;case wrio_event_type::input_removed:      wrui_handle_input_removed(ctx, event->input.device);
            break;case wrio_event_type::input_event:        wrui_handle_input(        ctx, event->input);
            break;case wrio_event_type::output_configure:   wrio_output_request_frame(event->output.output, ctx->render.usage);
            break;case wrio_event_type::output_redraw:      wrui_render(ctx, event->output.output, event->output.target);
            break;case wrio_event_type::output_added:
                  case wrio_event_type::output_removed:
                log_warn("wrio::{}", wrei_enum_to_string(event->type));
        }
    }
};

auto wrui_create(wren_context* wren, wrio_context* wrio) -> ref<wrui_context>
{
    auto wrui = wrei_create<wrui_context>();

    wrui->wren = wren;

    wrui->wrio = wrio;
    wrio_set_event_handler(wrio, event_handler{wrui.get()});

    wrui->root_tree = wrui_tree_create(wrui.get());

    for (auto layer : magic_enum::enum_values<wrui_layer>()) {
        auto* tree = (wrui->layers[layer] = wrui_tree_create(wrui.get())).get();
        wrui_node_set_transform(tree, wrui->root_transform.get());
        wrui_tree_place_above(wrui->root_tree.get(), nullptr, tree);
    }

    wrui->root_transform = wrui_transform_create(wrui.get());

    wrui_render_init(wrui.get());

    wrui->keyboard = wrui_keyboard_create(wrui.get());
    wrui->pointer = wrui_pointer_create(wrui.get());

    return wrui;
}

auto wrui_get_layer(wrui_context* ctx, wrui_layer layer) -> wrui_tree*
{
    return ctx->layers[layer].get();
}
auto wrui_get_root_transform(wrui_context* ctx) -> wrui_transform*
{
    return ctx->root_transform.get();
}

void wrui_broadcast_event(wrui_context* ctx, wrui_event* event)
{
    for (auto* client : ctx->clients) {
        wrui_client_post_event(client, event);
    }
}
