#include "server.hpp"

#include "renderer/renderer.hpp"
#include "renderer/vulkan_context.hpp"
#include "renderer/vulkan_helpers.hpp"

static
void output_init_swapchain(Output* output)
{
    auto* vk = output->server->renderer->vk;

    log_debug("Creating vulkan swapchain");
    vk_check(vkwsi_swapchain_create(&output->swapchain, output->server->renderer->vk->vkwsi, output->vk_surface));

    vk_check(vk->CreateSemaphore(vk->device, ptr_to(VkSemaphoreCreateInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = ptr_to(VkSemaphoreTypeCreateInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
            .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
            .initialValue = 0,
        }),
    }), nullptr, &output->timeline));

    std::vector<VkSurfaceFormatKHR> surface_formats;
    vk_enumerate(surface_formats, vk->GetPhysicalDeviceSurfaceFormatsKHR, vk->physical_device, output->vk_surface);
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

void output_added(Output* output)
{
    log_debug("Output added");

    if (!output->swapchain) {
        output_init_swapchain(output);
    }
}

void output_removed(Output* output)
{
    log_debug("Output removed");
    if (output->timeline) {
        output->server->renderer->vk->DestroySemaphore(output->server->renderer->vk->device, output->timeline, nullptr);
    }
    if (output->swapchain) {
        vkwsi_swapchain_destroy(output->swapchain);
    }
}

static
VkSemaphoreSubmitInfo output_get_next_submit_info(Output* output)
{
    return VkSemaphoreSubmitInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = output->timeline,
        .value = ++output->timeline_value,
    };
}

static
void vulkan_wait_for_timeline_value(VulkanContext* vk, const VkSemaphoreSubmitInfo& info)
{
    vk_check(vk->WaitSemaphores(vk->device, ptr_to(VkSemaphoreWaitInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores = &info.semaphore,
        .pValues = &info.value,
    }), UINT64_MAX));
}

vkwsi_swapchain_image output_acquire_image(Output* output)
{
    auto vk = output->server->renderer->vk;

    vkwsi_swapchain_resize(output->swapchain, {u32(output->size.x), u32(output->size.y)});

    auto timeline_info = output_get_next_submit_info(output);
    vk_check(vkwsi_swapchain_acquire(&output->swapchain, 1, vk->queue, &timeline_info, 1));
    vulkan_wait_for_timeline_value(vk, timeline_info);

    return vkwsi_swapchain_get_current(output->swapchain);
}
