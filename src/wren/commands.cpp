#include "internal.hpp"

ref<wren_queue> wren_queue_init(wren_context* ctx, wren_queue_type type, u32 family)
{
    auto queue = wrei_create<wren_queue>();
    queue->ctx = ctx;
    queue->type = type;
    queue->family = family;

    log_debug("Queue created of type \"{}\" with family {}", wrei_enum_to_string(type), family);

    ctx->vk.GetDeviceQueue(ctx->device, family, 0, &queue->queue);

    wren_check(ctx->vk.CreateCommandPool(ctx->device, wrei_ptr_to(VkCommandPoolCreateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = queue->family,
    }), nullptr, &queue->cmd_pool));

    queue->queue_sema = wren_semaphore_create(ctx);

    return queue;
}

wren_queue::~wren_queue()
{
    queue_sema = nullptr;

    ctx->vk.DestroyCommandPool(ctx->device, cmd_pool, nullptr);
}

wren_commands::~wren_commands()
{
    auto* ctx = queue->ctx;
    ctx->vk.FreeCommandBuffers(ctx->device, queue->cmd_pool, 1, &buffer);
}

wren_queue* wren_get_queue(wren_context* ctx, wren_queue_type type)
{
    switch (type) {
        break;case wren_queue_type::graphics:
            return ctx->graphics_queue.get();
        break;case wren_queue_type::transfer:
            return ctx->transfer_queue.get();
    }
}

ref<wren_commands> wren_commands_begin(wren_queue* queue)
{
    auto* ctx = queue->ctx;
    auto commands = wrei_create<wren_commands>();
    commands->queue = queue;

    wren_check(ctx->vk.AllocateCommandBuffers(ctx->device, wrei_ptr_to(VkCommandBufferAllocateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = queue->cmd_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    }), &commands->buffer));

    wren_check(ctx->vk.BeginCommandBuffer(commands->buffer, wrei_ptr_to(VkCommandBufferBeginInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    })));

    return commands;
}

void wren_commands_protect_object(wren_commands* commands, wrei_object* object)
{
    if (!object) return;
    commands->objects.emplace_back(object);
}

void wren_wait_idle(wren_context* ctx)
{
    wren_wait_idle(ctx->graphics_queue.get());
    wren_wait_idle(ctx->transfer_queue.get());
}

void wren_wait_idle(wren_queue* queue)
{
    queue->ctx->vk.QueueWaitIdle(queue->queue);
    wren_semaphore_wait_value(queue->queue_sema.get(), queue->submitted);
}

wren_syncpoint wren_commands_submit(wren_commands* commands, std::span<const wren_syncpoint> waits)
{
    auto* queue = commands->queue;
    auto* ctx = queue->ctx;

    wren_check(ctx->vk.EndCommandBuffer(commands->buffer));

    commands->submitted_value = ++queue->submitted;

    wren_syncpoint target_syncpoint {
        .semaphore = queue->queue_sema.get(),
        .value = commands->submitted_value,
    };

    std::vector<VkSemaphoreSubmitInfo> wait_infos(waits.size());
    for (auto[i, wait] : waits | std::views::enumerate) {
        wait_infos[i] = wren_syncpoint_to_submit_info(wait);
        wren_commands_protect_object(commands, wait.semaphore);
    }

    wren_check(ctx->vk.QueueSubmit2(queue->queue, 1, wrei_ptr_to(VkSubmitInfo2 {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = u32(wait_infos.size()),
        .pWaitSemaphoreInfos = wait_infos.data(),
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = wrei_ptr_to(VkCommandBufferSubmitInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = commands->buffer,
        }),
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos = wrei_ptr_to(wren_syncpoint_to_submit_info(target_syncpoint)),
    }), nullptr));

    wren_semaphore_wait_value(queue->queue_sema.get(), queue->submitted, [commands = ref(commands)](u64) {});

    return target_syncpoint;
}
