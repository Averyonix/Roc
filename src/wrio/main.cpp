#include "wrio.hpp"

static
void render(wrio_context* ctx, wrio_output* output, wren_image* image)
{
    auto wren = wrio_context_get_wren(ctx);

    auto queue = wren_get_queue(wren, wren_queue_type::graphics);
    auto commands = wren_commands_begin(queue);

    wren->vk.CmdClearColorImage(commands->buffer, image->image, VK_IMAGE_LAYOUT_GENERAL,
        wrei_ptr_to(VkClearColorValue{.float32{1,0,0,1}}),
        1, wrei_ptr_to(VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}));

    auto done = wren_commands_submit(commands.get(), {});
    wrio_output_present(output, image, done);
}

static
void handle_event(wrio_event* event)
{
    auto& input = event->input;

    switch (event->type) {
        break;case wrio_event_type::shutdown_requested:
            log_error("wrio::shutdown_requested({})", wrei_enum_to_string(event->shutdown.reason));
            wrio_context_stop(event->ctx);
        break;case wrio_event_type::input_leave:
            log_info("wrio::input_leave()");
        break;case wrio_event_type::input_key_enter:
            log_info("wrio::input_key_enter({})", libevdev_event_code_get_name(EV_KEY, input.key));
        break;case wrio_event_type::input_key_press:
            log_info("wrio::input_key_press({})", libevdev_event_code_get_name(EV_KEY, input.key));
        break;case wrio_event_type::input_key_release:
            log_info("wrio::input_key_release({})", libevdev_event_code_get_name(EV_KEY, input.key));
        break;case wrio_event_type::input_pointer_motion:
            log_info("wrio::input_pointer_motion{}", wrei_to_string(input.motion));
        break;case wrio_event_type::input_pointer_axis:
            log_info("wrio::input_pointer_axis({}, {})", wrei_enum_to_string(input.axis.axis), input.axis.delta);
        break;case wrio_event_type::output_configure:
            log_info("wrio::output_configure{}", wrei_to_string(wrio_output_get_size(event->output.output)));
            wrio_output_request_frame(event->output.output, wren_image_usage::transfer_dst);
        break;case wrio_event_type::output_redraw:
            render(event->ctx, event->output.output, event->output.target);
        break;case wrio_event_type::input_added:
              case wrio_event_type::input_removed:
              case wrio_event_type::output_added:
              case wrio_event_type::output_removed:
            log_warn("wrio::{}", wrei_enum_to_string(event->type));
    }
}

int main()
{
    auto wrio = wrio_context_create();
    wrio_context_set_event_handler(wrio.get(), handle_event);
    wrio_context_run(wrio.get());
}
