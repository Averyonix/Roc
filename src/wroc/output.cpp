#include "server.hpp"

#include "wren/wren.hpp"
#include "wren/wren_helpers.hpp"

static
void wroc_output_init_swapchain(wroc_output* output)
{
    auto* wren = output->server->renderer->wren.get();

    log_debug("Creating vulkan swapchain");
    wren_check(vkwsi_swapchain_create(&output->swapchain, output->server->renderer->wren->vkwsi, output->vk_surface));

    wren_check(wren->vk.CreateSemaphore(wren->device, wrei_ptr_to(VkSemaphoreCreateInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = wrei_ptr_to(VkSemaphoreTypeCreateInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
            .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
            .initialValue = 0,
        }),
    }), nullptr, &output->timeline));

    std::vector<VkSurfaceFormatKHR> surface_formats;
    wren_vk_enumerate(surface_formats, wren->vk.GetPhysicalDeviceSurfaceFormatsKHR, wren->physical_device, output->vk_surface);
    for (auto& f : surface_formats) {
        // if (f.format == VK_FORMAT_R8G8B8A8_SRGB || f.format == VK_FORMAT_B8G8R8A8_SRGB) {
        if (f.format == VK_FORMAT_R8G8B8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_UNORM) {
            output->format = f;
            break;
        }
    }

    auto sw_info = vkwsi_swapchain_info_default();
    sw_info.image_sharing_mode = VK_SHARING_MODE_EXCLUSIVE;
    sw_info.image_usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sw_info.present_mode = VK_PRESENT_MODE_FIFO_KHR;
    sw_info.min_image_count = 2;
    sw_info.format = output->format.format;
    sw_info.color_space = output->format.colorSpace;
    vkwsi_swapchain_set_info(output->swapchain, &sw_info);
}

void wroc_output_added(wroc_output* output)
{
    log_debug("Output added");

    if (!output->swapchain) {
        wroc_output_init_swapchain(output);
    }
}

void wroc_output_removed(wroc_output* output)
{
    log_debug("Output removed");
    if (output->timeline) {
        output->server->renderer->wren->vk.DestroySemaphore(output->server->renderer->wren->device, output->timeline, nullptr);
    }
    if (output->swapchain) {
        vkwsi_swapchain_destroy(output->swapchain);
    }
}

static
VkSemaphoreSubmitInfo wroc_output_get_next_submit_info(wroc_output* output)
{
    return VkSemaphoreSubmitInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = output->timeline,
        .value = ++output->timeline_value,
    };
}

vkwsi_swapchain_image wroc_output_acquire_image(wroc_output* output)
{

    auto* wren = output->server->renderer->wren.get();
    vkwsi_swapchain_resize(output->swapchain, {u32(output->size.x), u32(output->size.y)});

    auto timeline_info = wroc_output_get_next_submit_info(output);
    wren_check(vkwsi_swapchain_acquire(&output->swapchain, 1, wren->queue, &timeline_info, 1));
    wren_wait_for_timeline_value(wren, timeline_info);

    return vkwsi_swapchain_get_current(output->swapchain);
}
