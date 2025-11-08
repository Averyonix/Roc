#include "vulkan_context.hpp"
#include "vulkan_helpers.hpp"

#include "common/types.hpp"
#include "common/util.hpp"

void vulkan_context_destroy(VulkanContext* vk)
{
    if (!vk) return;

    vk->DestroyCommandPool(vk->device, vk->cmd_pool, nullptr);
    vk->DestroyDevice(vk->device, nullptr);
    vk->DestroyInstance(vk->instance, nullptr);

    delete vk;
}

VkCommandBuffer vulkan_context_begin_commands(VulkanContext* vk)
{
    VkCommandBuffer cmd;
    vk_check(vk->AllocateCommandBuffers(vk->device, ptr_to(VkCommandBufferAllocateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vk->cmd_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    }), &cmd));

    vk_check(vk->BeginCommandBuffer(cmd, ptr_to(VkCommandBufferBeginInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    })));

    return cmd;
}

void vulkan_context_submit_commands(VulkanContext* vk, VkCommandBuffer cmd)
{
    defer { vk->FreeCommandBuffers(vk->device, vk->cmd_pool, 1, &cmd); };

    vk_check(vk->EndCommandBuffer(cmd));

    vk_check(vk->QueueSubmit2(vk->queue, 1, ptr_to(VkSubmitInfo2 {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = ptr_to(VkCommandBufferSubmitInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = cmd,
        })
    }), nullptr));
    vk_check(vk->QueueWaitIdle(vk->queue));
}
