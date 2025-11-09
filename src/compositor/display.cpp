#include "display.hpp"

#include "renderer/renderer.hpp"
#include "renderer/vulkan_context.hpp"
#include "renderer/vulkan_helpers.hpp"

void display_run(int /* argc */, char* /* argv */[])
{
    Display display = {};

    display.event_loop = event_loop_create();

    backend_init(&display);
    renderer_init(&display);

    log_info("Running compositor");

    event_loop_run(display.event_loop);

    log_info("Compositor shutting down");

    if (display.backend) {
        backend_destroy(display.backend);
    }
}

void output_added(Output* /* output */)
{
    log_debug("Output added");
}

void output_removed(Output* /* output */)
{
    log_debug("Output removed");
}

void output_frame(Output* output)
{
    auto* vk = output->display->renderer->vk;
    auto cmd = vulkan_context_begin_commands(vk);

    log_info("acquiring image");

    auto current = output_acquire_image(output);
    log_info("Rendering frame ({}, {})", current.extent.width, current.extent.height);

    vk_transition(vk, cmd, current.image,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        0, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    vk->CmdClearColorImage(cmd, current.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        ptr_to(VkClearColorValue{.float32{0.1f, 0.1f, 0.1f, 1.f}}),
        1, ptr_to(VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}));

    vk_transition(vk, cmd, current.image,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, 0,
        VK_ACCESS_2_TRANSFER_WRITE_BIT, 0,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    vulkan_context_submit_commands(vk, cmd);
    vk_check(vkwsi_swapchain_present(&output->swapchain, 1, vk->queue, nullptr, 0, false));
}
