#include "io.hpp"

static
void render(gpu_context* gpu, io_output* output, gpu_image* image)
{
    auto queue = gpu_get_queue(gpu, gpu_queue_type::graphics);
    auto commands = gpu_commands_begin(queue);

    gpu->vk.CmdClearColorImage(commands->buffer, image->image, VK_IMAGE_LAYOUT_GENERAL,
        ptr_to(VkClearColorValue{.float32{1,0,0,1}}),
        1, ptr_to(VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}));

    auto done = gpu_commands_submit(commands.get(), {});
    io_output_present(output, image, done);
}

static
void handle_event(gpu_context* gpu, io_event* event)
{
    auto& input = event->input;

    switch (event->type) {
        break;case io_event_type::shutdown_requested:
            log_error("io::shutdown_requested({})", core_enum_to_string(event->shutdown.reason));
            io_stop(event->ctx);
        break;case io_event_type::input_event:
            static constexpr auto channel_to_str = [](auto& e) {
                return std::format("{} = {}", libevdev_event_code_get_name(e.type, e.code), e.value);
            };
            log_info("io::input_event({:s}{})",
                input.channels | std::views::transform(channel_to_str) | std::views::join_with(", "sv),
                input.quiet ? ", QUIET" : "");
        break;case io_event_type::output_configure:
            log_info("io::output_configure{}", core_to_string(io_output_get_size(event->output.output)));
            io_output_request_frame(event->output.output, gpu_image_usage::transfer_dst);
        break;case io_event_type::output_redraw:
            render(gpu, event->output.output, event->output.target);
        break;case io_event_type::input_added:
              case io_event_type::input_removed:
              case io_event_type::output_added:
              case io_event_type::output_removed:
            log_warn("io::{}", core_enum_to_string(event->type));
    }
}

int main()
{
    auto event_loop = core_event_loop_create();
    auto gpu = gpu_create({}, event_loop.get());
    auto io = io_create(event_loop.get(), gpu.get());
    io_set_event_handler(io.get(), [gpu = gpu.get()](io_event* event) {
        handle_event(gpu, event);
    });
    io_run(io.get());
}
