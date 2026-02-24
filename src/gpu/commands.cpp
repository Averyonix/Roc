#include "internal.hpp"

ref<gpu_queue> gpu_queue_init(gpu_context* ctx, gpu_queue_type type, u32 family)
{
    auto queue = core_create<gpu_queue>();
    queue->ctx = ctx;
    queue->type = type;
    queue->family = family;

    log_debug("Queue created of type \"{}\" with family {}", core_enum_to_string(type), family);

    ctx->vk.GetDeviceQueue(ctx->device, family, 0, &queue->queue);

    gpu_check(ctx->vk.CreateCommandPool(ctx->device, ptr_to(VkCommandPoolCreateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = queue->family,
    }), nullptr, &queue->cmd_pool));

    queue->queue_sema = gpu_semaphore_create(ctx);

    return queue;
}

gpu_queue::~gpu_queue()
{
    queue_sema = nullptr;

    ctx->vk.DestroyCommandPool(ctx->device, cmd_pool, nullptr);
}

gpu_commands::~gpu_commands()
{
    auto* ctx = queue->ctx;
    ctx->vk.FreeCommandBuffers(ctx->device, queue->cmd_pool, 1, &buffer);
}

gpu_queue* gpu_get_queue(gpu_context* ctx, gpu_queue_type type)
{
    switch (type) {
        break;case gpu_queue_type::graphics:
            return ctx->graphics_queue.get();
        break;case gpu_queue_type::transfer:
            return ctx->transfer_queue.get();
    }
}

ref<gpu_commands> gpu_commands_begin(gpu_queue* queue)
{
    auto* ctx = queue->ctx;
    auto commands = core_create<gpu_commands>();
    commands->queue = queue;

    gpu_check(ctx->vk.AllocateCommandBuffers(ctx->device, ptr_to(VkCommandBufferAllocateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = queue->cmd_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    }), &commands->buffer));

    gpu_check(ctx->vk.BeginCommandBuffer(commands->buffer, ptr_to(VkCommandBufferBeginInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    })));

    return commands;
}

void gpu_commands_protect_object(gpu_commands* commands, core_object* object)
{
    if (!object) return;
    commands->objects.emplace_back(object);
}

void gpu_wait_idle(gpu_context* ctx)
{
    gpu_wait_idle(ctx->graphics_queue.get());
    gpu_wait_idle(ctx->transfer_queue.get());
}

void gpu_wait_idle(gpu_queue* queue)
{
    queue->ctx->vk.QueueWaitIdle(queue->queue);
    gpu_semaphore_wait_value(queue->queue_sema.get(), queue->submitted);
}

gpu_syncpoint gpu_commands_submit(gpu_commands* commands, std::span<const gpu_syncpoint> waits)
{
    auto* queue = commands->queue;
    auto* ctx = queue->ctx;

    gpu_check(ctx->vk.EndCommandBuffer(commands->buffer));

    commands->submitted_value = ++queue->submitted;

    gpu_syncpoint target_syncpoint {
        .semaphore = queue->queue_sema.get(),
        .value = commands->submitted_value,
    };

    std::vector<VkSemaphoreSubmitInfo> wait_infos(waits.size());
    for (auto[i, wait] : waits | std::views::enumerate) {
        wait_infos[i] = gpu_syncpoint_to_submit_info(wait);
        gpu_commands_protect_object(commands, wait.semaphore);
    }

    gpu_check(ctx->vk.QueueSubmit2(queue->queue, 1, ptr_to(VkSubmitInfo2 {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = u32(wait_infos.size()),
        .pWaitSemaphoreInfos = wait_infos.data(),
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = ptr_to(VkCommandBufferSubmitInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = commands->buffer,
        }),
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos = ptr_to(gpu_syncpoint_to_submit_info(target_syncpoint)),
    }), nullptr));

    gpu_semaphore_wait_value(queue->queue_sema.get(), queue->submitted, [commands = ref(commands)](u64) {});

    return target_syncpoint;
}
