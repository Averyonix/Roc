#include "wrio.hpp"

static
void render(wren_context* wren, wrio_output* output, wren_image* image)
{
    auto queue = wren_get_queue(wren, wren_queue_type::graphics);
    auto commands = wren_commands_begin(queue);

    wren->vk.CmdClearColorImage(commands->buffer, image->image, VK_IMAGE_LAYOUT_GENERAL,
        wrei_ptr_to(VkClearColorValue{.float32{1,0,0,1}}),
        1, wrei_ptr_to(VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}));

    auto done = wren_commands_submit(commands.get(), {});
    wrio_output_present(output, image, done);
}

static
void handle_event(wren_context* wren, wrio_event* event)
{
    auto& input = event->input;

    switch (event->type) {
        break;case wrio_event_type::shutdown_requested:
            log_error("wrio::shutdown_requested({})", wrei_enum_to_string(event->shutdown.reason));
            wrio_stop(event->ctx);
        break;case wrio_event_type::input_event:
            static constexpr auto channel_to_str = [](auto& e) {
                return std::format("{} = {}", libevdev_event_code_get_name(e.type, e.code), e.value);
            };
            log_info("wrio::input_event({:s}{})",
                input.channels | std::views::transform(channel_to_str) | std::views::join_with(", "sv),
                input.quiet ? ", QUIET" : "");
        break;case wrio_event_type::output_configure:
            log_info("wrio::output_configure{}", wrei_to_string(wrio_output_get_size(event->output.output)));
            wrio_output_request_frame(event->output.output, wren_image_usage::transfer_dst);
        break;case wrio_event_type::output_redraw:
            render(wren, event->output.output, event->output.target);
        break;case wrio_event_type::input_added:
              case wrio_event_type::input_removed:
              case wrio_event_type::output_added:
              case wrio_event_type::output_removed:
            log_warn("wrio::{}", wrei_enum_to_string(event->type));
    }
}

int main()
{
    auto event_loop = wrei_event_loop_create();
    auto wren = wren_create({}, event_loop.get());
    auto wrio = wrio_create(event_loop.get(), wren.get());
    wrio_set_event_handler(wrio.get(), [wren = wren.get()](wrio_event* event) {
        handle_event(wren, event);
    });
    wrio_run(wrio.get());
}
