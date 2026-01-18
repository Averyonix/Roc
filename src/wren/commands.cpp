#include "internal.hpp"

static
void reclaim_old_submissions(wren_queue* queue)
{
    auto value = wren_semaphore_get_value(queue->queue_sema.get());

    while (!queue->submissions.empty()) {
        if (queue->submissions.front()->submitted_value <= value) {
            // log_debug("reclaiming submission, value = {}", ctx->submissions.front()->submitted_value);
            queue->submissions.pop_front();
        } else {
            break;
        }
    }
}

static
void handle_signalled(wren_queue* queue, u64 value)
{
    queue->queue_sema->observed = std::max(queue->queue_sema->observed, value);
    reclaim_old_submissions(queue);
}

static
void wait_thread(wren_queue* queue)
{
    auto* ctx = queue->ctx;
    auto* semaphore = queue->queue_sema.get();

    u64 observed = 0;
    for (;;) {
        queue->wait_thread_submitted.wait(observed);
        auto wait_value = queue->wait_thread_submitted.load();

        if (wait_value == UINT64_MAX) {
            return;
        }

        // TODO: Thread safe wren_semaphore_* commands?

        wren_check(ctx->vk.WaitSemaphores(ctx->device, wrei_ptr_to(VkSemaphoreWaitInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
            .semaphoreCount = 1,
            .pSemaphores = &semaphore->semaphore,
            .pValues = &wait_value,
        }), UINT64_MAX));

        wren_check(ctx->vk.GetSemaphoreCounterValue(ctx->device, semaphore->semaphore, &observed));

        wrei_event_loop_enqueue(ctx->event_loop.get(), [queue, observed] {
            handle_signalled(queue, observed);
        });
    }
}

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

    queue->queue_sema = wren_semaphore_create(ctx, VK_SEMAPHORE_TYPE_TIMELINE);

    queue->wait_thread = std::jthread([queue = queue.get()] {
        wait_thread(queue);
    });

    return queue;
}

wren_queue::~wren_queue()
{
    assert(submissions.empty());

    wait_thread_submitted = UINT64_MAX;
    wait_thread_submitted.notify_one();
    wait_thread.join();

    queue_sema = nullptr;

    ctx->vk.DestroyCommandPool(ctx->device, cmd_pool, nullptr);
}

void wren_commands_init(wren_context* ctx)
{
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
    wren_semaphore_wait_value(queue->queue_sema.get(), queue->queue_sema->submitted);
    reclaim_old_submissions(queue);
}

void wren_commands_submit(wren_commands* commands, std::span<const wren_syncpoint> waits, std::span<const wren_syncpoint> signals)
{
    auto* queue = commands->queue;
    auto* ctx = queue->ctx;

    wren_check(ctx->vk.EndCommandBuffer(commands->buffer));

    commands->submitted_value = wren_semaphore_advance(queue->queue_sema.get());

    auto wait_infos = wren_syncpoints_to_submit_infos(waits);
    auto signal_infos = wren_syncpoints_to_submit_infos(signals, wrei_ptr_to(wren_syncpoint {
        .semaphore = queue->queue_sema.get(),
        .value = commands->submitted_value,
    }));

    for (auto& s : waits) wren_commands_protect_object(commands, s.semaphore);
    for (auto& s : signals) wren_commands_protect_object(commands, s.semaphore);

    queue->wait_thread_submitted = commands->submitted_value;

    wren_check(ctx->vk.QueueSubmit2(queue->queue, 1, wrei_ptr_to(VkSubmitInfo2 {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = u32(wait_infos.size()),
        .pWaitSemaphoreInfos = wait_infos.data(),
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = wrei_ptr_to(VkCommandBufferSubmitInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = commands->buffer,
        }),
        .signalSemaphoreInfoCount = u32(signal_infos.size()),
        .pSignalSemaphoreInfos = signal_infos.data(),
    }), nullptr));

    queue->submissions.emplace_back(commands);

    // Notify wait thread of new value to wait on

    queue->wait_thread_submitted.notify_one();
}
