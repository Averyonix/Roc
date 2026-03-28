#include "internal.hpp"

#include "core/enum.hpp"
#include "core/stack.hpp"

void gpu_queue_init(Gpu* gpu)
{
    gpu->vk.GetDeviceQueue(gpu->device, gpu->queue.family, 0, &gpu->queue.queue);

    gpu_check(gpu->vk.CreateCommandPool(gpu->device, ptr_to(VkCommandPoolCreateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = gpu->queue.family,
    }), nullptr, &gpu->queue.pool));

    gpu->queue.syncobj = gpu_syncobj_create(gpu);
}

// -----------------------------------------------------------------------------

GpuCommands::~GpuCommands()
{
    gpu->vk.FreeCommandBuffers(gpu->device, gpu->queue.pool, 1, &buffer);
}

auto gpu_get_commands(Gpu* gpu) -> GpuCommands*
{
    if (gpu->queue.commands) return gpu->queue.commands.get();

    auto commands = ref_create<GpuCommands>();
    commands->gpu = gpu;

    gpu_check(gpu->vk.AllocateCommandBuffers(gpu->device, ptr_to(VkCommandBufferAllocateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = gpu->queue.pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    }), &commands->buffer));

    gpu_check(gpu->vk.BeginCommandBuffer(commands->buffer, ptr_to(VkCommandBufferBeginInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    })));

    gpu->queue.commands = std::move(commands);

    return gpu->queue.commands.get();
}

// -----------------------------------------------------------------------------

void gpu_protect(Gpu* gpu, Ref<void> object)
{
    if (!object) return;
    gpu_get_commands(gpu)->objects.emplace_back(std::move(object));
}

// -----------------------------------------------------------------------------

GpuSyncpoint gpu_flush(Gpu* gpu)
{
    auto* commands = gpu->queue.commands.get();

    gpu_check(gpu->vk.EndCommandBuffer(commands->buffer));

    commands->submitted_value = ++gpu->queue.submitted;

    GpuSyncpoint target {
        .syncobj = gpu->queue.syncobj.get(),
        .value = commands->submitted_value,
    };

    gpu_check(gpu->vk.QueueSubmit2(gpu->queue.queue, 1,
        ptr_to(VkSubmitInfo2 {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = ptr_to(VkCommandBufferSubmitInfo {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                .commandBuffer = commands->buffer,
            }),
            .signalSemaphoreInfoCount = 1,
            .pSignalSemaphoreInfos = ptr_to(gpu_syncpoint_to_submit_info(target)),
        }),
        nullptr));

    gpu_wait({gpu->queue.syncobj.get(), gpu->queue.submitted}, [commands = Ref(commands)](u64) {});

    gpu->queue.commands = nullptr;

    return target;
}
