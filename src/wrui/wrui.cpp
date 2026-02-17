#include "internal.hpp"

WREI_OBJECT_EXPLICIT_DEFINE(wrui_context);
WREI_OBJECT_EXPLICIT_DEFINE(wrui_window);

static
void handle_wrio_event(wrui_context* ctx, struct wrio_event* event)
{
    auto& input = event->input;

    switch (event->type) {
        break;case wrio_event_type::shutdown_requested:
            log_error("wrio::shutdown_requested({})", wrei_enum_to_string(event->shutdown.reason));
            wrio_stop(event->ctx);
        break;case wrio_event_type::input_leave:
            log_info("wrio::input_leave()");
        break;case wrio_event_type::input_key_enter:
            log_info("wrio::input_key_enter({})", libevdev_event_code_get_name(EV_KEY, input.key));
        break;case wrio_event_type::input_key_press:
            log_info("wrio::input_key_press({})", libevdev_event_code_get_name(EV_KEY, input.key));
        break;case wrio_event_type::input_key_release:
            log_info("wrio::input_key_release({})", libevdev_event_code_get_name(EV_KEY, input.key));
        break;case wrio_event_type::input_pointer_motion:
            // log_info("wrio::input_pointer_motion{}", wrei_to_string(input.motion));
        break;case wrio_event_type::input_pointer_axis:
            log_info("wrio::input_pointer_axis({}, {})", wrei_enum_to_string(input.axis.axis), input.axis.delta);
        break;case wrio_event_type::output_configure:
            log_info("wrio::output_configure{}", wrei_to_string(wrio_output_get_size(event->output.output)));
            wrio_output_request_frame(event->output.output, wren_image_usage::render);
        break;case wrio_event_type::output_redraw:
            wrui_render(ctx, event->output.output, event->output.target);
        break;case wrio_event_type::input_added:
              case wrio_event_type::input_removed:
              case wrio_event_type::output_added:
              case wrio_event_type::output_removed:
            log_warn("wrio::{}", wrei_enum_to_string(event->type));
    }
}

auto wrui_create(wren_context* wren, wrio_context* wrio) -> ref<wrui_context>
{
    auto wrui = wrei_create<wrui_context>();

    wrui->wren = wren;
    wrio_set_event_handler(wrio, [&](wrio_event* event) {
        handle_wrio_event(wrui.get(), event);
    });

    wrui->scene = wrui_tree_create(wrui.get());
    wrui->root_transform = wrui_transform_create(wrui.get());

    wrui_render_init(wrui.get());

    return wrui;
}

auto wrui_get_scene(wrui_context* ctx) -> wrui_scene
{
    return { ctx->scene.get(), ctx->root_transform.get() };
}

auto wrui_window_create(wrui_context* ctx) -> ref<wrui_window>
{
    auto window = wrei_create<wrui_window>();
    window->ctx = ctx;

    window->transform = wrui_transform_create(ctx);
    wrui_node_set_transform(window->transform.get(), ctx->root_transform.get());

    window->tree = wrui_tree_create(ctx);
    wrui_tree_place_above(ctx->scene.get(), nullptr, window->tree.get());

    window->decorations = wrui_tree_create(ctx);
    wrui_tree_place_above(window->tree.get(), nullptr, window->decorations.get());

    return window;
}
