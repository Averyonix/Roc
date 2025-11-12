#include "wren.hpp"
#include "wren_helpers.hpp"

#include "wrei/types.hpp"
#include "wrei/util.hpp"

void wren_destroy(wren_context* ctx)
{
    if (!ctx) return;

    ctx->vk.DestroyCommandPool(ctx->device, ctx->cmd_pool, nullptr);
    ctx->vk.DestroyDevice(ctx->device, nullptr);
    ctx->vk.DestroyInstance(ctx->instance, nullptr);

    delete ctx;
}

VkCommandBuffer wren_begin_commands(wren_context* ctx)
{
    VkCommandBuffer cmd;
    wren_check(ctx->vk.AllocateCommandBuffers(ctx->device, wrei_ptr_to(VkCommandBufferAllocateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = ctx->cmd_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    }), &cmd));

    wren_check(ctx->vk.BeginCommandBuffer(cmd, wrei_ptr_to(VkCommandBufferBeginInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    })));

    return cmd;
}

void wren_submit_commands(wren_context* ctx, VkCommandBuffer cmd)
{
    defer { ctx->vk.FreeCommandBuffers(ctx->device, ctx->cmd_pool, 1, &cmd); };

    wren_check(ctx->vk.EndCommandBuffer(cmd));

    wren_check(ctx->vk.QueueSubmit2(ctx->queue, 1, wrei_ptr_to(VkSubmitInfo2 {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = wrei_ptr_to(VkCommandBufferSubmitInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = cmd,
        })
    }), nullptr));
    wren_check(ctx->vk.QueueWaitIdle(ctx->queue));
}
