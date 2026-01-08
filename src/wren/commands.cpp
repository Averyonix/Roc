#include "internal.hpp"

static
void reclaim_old_submissions(wren_context* ctx)
{
    auto value = wren_semaphore_get_value(ctx->queue_sema.get());

    while (!ctx->submissions.empty()) {
        if (ctx->submissions.front()->submitted_value <= value) {
            // log_debug("reclaiming submission, value = {}", ctx->submissions.front()->submitted_value);
            ctx->submissions.pop_front();
        } else {
            break;
        }
    }
}

static
void handle_signalled(wren_context* ctx, u64 value)
{
    ctx->queue_sema->observed = std::max(ctx->queue_sema->observed, value);
    reclaim_old_submissions(ctx);
}

static
void wait_thread(wren_context* ctx)
{
    auto* semaphore = ctx->queue_sema.get();

    u64 observed = 0;
    for (;;) {
        ctx->wait_thread_submitted.wait(observed);
        auto wait_value = ctx->wait_thread_submitted.load();

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

        wrei_event_loop_enqueue(ctx->event_loop.get(), [ctx, observed] {
            handle_signalled(ctx, observed);
        });
    }
}

void wren_commands_shutdown(wren_context* ctx)
{
    ctx->wait_thread_submitted = UINT64_MAX;
    ctx->wait_thread_submitted.notify_one();
    ctx->wait_thread.join();
}

void wren_commands_init(wren_context* ctx)
{
    ctx->wait_thread = std::jthread([ctx] {
        wait_thread(ctx);
    });
}

wren_commands::~wren_commands()
{
    ctx->vk.FreeCommandBuffers(ctx->device, ctx->cmd_pool, 1, &buffer);
}

ref<wren_commands> wren_commands_begin(wren_context* ctx)
{
    auto commands = wrei_create<wren_commands>();
    commands->ctx = ctx;

    wren_check(ctx->vk.AllocateCommandBuffers(ctx->device, wrei_ptr_to(VkCommandBufferAllocateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = ctx->cmd_pool,
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
    wren_semaphore_wait_value(ctx->queue_sema.get(), ctx->queue_sema->submitted);
    reclaim_old_submissions(ctx);
}

void wren_commands_submit(wren_commands* commands, std::span<const wren_syncpoint> waits, std::span<const wren_syncpoint> signals)
{
    auto* ctx = commands->ctx;

    wren_check(ctx->vk.EndCommandBuffer(commands->buffer));

    commands->submitted_value = wren_semaphore_advance(ctx->queue_sema.get());

    auto wait_infos = wren_syncpoints_to_submit_infos(waits);
    auto signal_infos = wren_syncpoints_to_submit_infos(signals, wrei_ptr_to(wren_syncpoint {
        .semaphore = ctx->queue_sema.get(),
        .value = commands->submitted_value,
    }));

    for (auto& s : waits) wren_commands_protect_object(commands, s.semaphore);
    for (auto& s : signals) wren_commands_protect_object(commands, s.semaphore);

    ctx->wait_thread_submitted = commands->submitted_value;

    wren_check(ctx->vk.QueueSubmit2(ctx->queue, 1, wrei_ptr_to(VkSubmitInfo2 {
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

    ctx->submissions.emplace_back(commands);

    // Notify wait thread of new value to wait on

    ctx->wait_thread_submitted.notify_one();
}
