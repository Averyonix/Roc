#include "internal.hpp"

#include "core/enum.hpp"
#include "core/stack.hpp"

ref<gpu_queue> gpu_queue_init(gpu_context* gpu, gpu_queue_type type, u32 family)
{
    auto queue = core_create<gpu_queue>();
    queue->gpu = gpu;
    queue->type = type;
    queue->family = family;

    log_debug("Queue created of type \"{}\" with family {}", core_to_string(type), family);

    gpu->vk.GetDeviceQueue(gpu->device, family, 0, &queue->queue);

    gpu_check(gpu->vk.CreateCommandPool(gpu->device, ptr_to(VkCommandPoolCreateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = queue->family,
    }), nullptr, &queue->cmd_pool));

    queue->syncobj = gpu_syncobj_create(gpu);

    return queue;
}

gpu_queue::~gpu_queue()
{
    syncobj = nullptr;

    gpu->vk.DestroyCommandPool(gpu->device, cmd_pool, nullptr);
}

gpu_queue* gpu_get_queue(gpu_context* gpu, gpu_queue_type type)
{
    switch (type) {
        break;case gpu_queue_type::graphics:
            return gpu->graphics_queue.get();
        break;case gpu_queue_type::transfer:
            return gpu->transfer_queue.get();
    }

    core_unreachable();
}

// -----------------------------------------------------------------------------

gpu_commands::~gpu_commands()
{
    auto* gpu = queue->gpu;
    gpu->vk.FreeCommandBuffers(gpu->device, queue->cmd_pool, 1, &buffer);

#if GPU_VALIDATION_COMPATIBILITY
    if (validation.fence) {
        gpu->vk.DestroyFence(gpu->device, validation.fence, nullptr);
    }
#endif
}

ref<gpu_commands> gpu_begin(gpu_queue* queue)
{
    auto* gpu = queue->gpu;
    auto commands = core_create<gpu_commands>();
    commands->queue = queue;

    gpu_check(gpu->vk.AllocateCommandBuffers(gpu->device, ptr_to(VkCommandBufferAllocateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = queue->cmd_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    }), &commands->buffer));

    gpu_check(gpu->vk.BeginCommandBuffer(commands->buffer, ptr_to(VkCommandBufferBeginInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    })));

    return commands;
}

// -----------------------------------------------------------------------------

void gpu_protect(gpu_commands* commands, ref<void> object)
{
    if (!object) return;
    commands->objects.emplace_back(std::move(object));
}

// -----------------------------------------------------------------------------

void gpu_wait_idle(gpu_context* gpu)
{
    gpu_wait_idle(gpu->graphics_queue.get());
    gpu_wait_idle(gpu->transfer_queue.get());
}

void gpu_wait_idle(gpu_queue* queue)
{
    queue->gpu->vk.QueueWaitIdle(queue->queue);
    gpu_wait({queue->syncobj.get(), queue->submitted});
}

// -----------------------------------------------------------------------------

static
void transfer(gpu_binary_semaphore* from, gpu_syncobj* to, u64 to_point)
{
    auto gpu = from->gpu;

    int syncobj_fd = -1;
    gpu_check(gpu->vk.GetSemaphoreFdKHR(gpu->device, ptr_to(VkSemaphoreGetFdInfoKHR {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
        .semaphore = from->semaphore,
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
    }), &syncobj_fd));

    gpu_syncobj_import_syncfile(to, syncobj_fd, to_point);
    close(syncobj_fd);
}

static
auto submit_info(gpu_binary_semaphore* semaphore, VkPipelineStageFlags2 stages) -> VkSemaphoreSubmitInfo
{
    return {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = semaphore->semaphore,
        .stageMask = stages,
    };
}

static
auto get_wait(gpu_commands* commands, gpu_syncpoint syncpoint) -> VkSemaphoreSubmitInfo
{
    auto gpu = commands->queue->gpu;
    auto binary = gpu_get_binary_semaphore(gpu);

    auto sync_fd = gpu_syncobj_export_syncfile(syncpoint.syncobj, syncpoint.value);

    gpu_check(gpu->vk.ImportSemaphoreFdKHR(gpu->device, ptr_to(VkImportSemaphoreFdInfoKHR {
        .sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,
        .semaphore = binary->semaphore,
        .flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT,
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
        .fd = sync_fd,
    })));

    gpu_protect(commands, binary.get());
    return submit_info(binary.get(), syncpoint.stages);
}

gpu_syncpoint gpu_submit(gpu_commands* commands, std::span<const gpu_syncpoint> waits)
{
    auto* queue = commands->queue;
    auto* gpu = queue->gpu;

    core_thread_stack stack;

    gpu_check(gpu->vk.EndCommandBuffer(commands->buffer));

    commands->submitted_value = ++queue->submitted;

    gpu_syncpoint target {
        .syncobj = queue->syncobj.get(),
        .value = commands->submitted_value,
    };

    auto* wait_infos = stack.allocate<VkSemaphoreSubmitInfo>(waits.size());
    for (auto[i, wait] : waits | std::views::enumerate) {
        wait_infos[i] = get_wait(commands, wait);
    }

    // Signal does not require protection, as exporting resets its payload.
    auto signal = gpu_get_binary_semaphore(gpu);

#if GPU_VALIDATION_COMPATIBILITY
    if (gpu->features.contains(gpu_feature::validation)) {
        // When validation layers are enabled, they need visibility of command completion
        // otherwise they will complain about resources still being used.
        gpu_check(gpu->vk.CreateFence(gpu->device, ptr_to(VkFenceCreateInfo {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        }), nullptr, &commands->validation.fence));
    }
#endif

    gpu_check(gpu->vk.QueueSubmit2(queue->queue, 1,
        ptr_to(VkSubmitInfo2 {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .waitSemaphoreInfoCount = u32(waits.size()),
            .pWaitSemaphoreInfos = wait_infos,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = ptr_to(VkCommandBufferSubmitInfo {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                .commandBuffer = commands->buffer,
            }),
            .signalSemaphoreInfoCount = 1,
            .pSignalSemaphoreInfos = ptr_to(submit_info(signal.get(), VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT)),
        }),
#if GPU_VALIDATION_COMPATIBILITY
        commands->validation.fence
#else
        nullptr
#endif
        ));

    transfer(signal.get(), target.syncobj, target.value);

    gpu_wait({queue->syncobj.get(), queue->submitted}, [commands = ref(commands)](u64) {
#if GPU_VALIDATION_COMPATIBILITY
        if (commands->validation.fence) {
            auto* gpu = commands->queue->gpu;
            gpu_check(gpu->vk.WaitForFences(gpu->device, 1, &commands->validation.fence, true, UINT64_MAX));
        }
#endif
    });

    return target;
}
