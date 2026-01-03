#include "internal.hpp"

ref<wren_semaphore> wren_semaphore_create(wren_context* ctx, VkSemaphoreType type, u64 initial_value)
{
    auto semaphore = wrei_create<wren_semaphore>();
    semaphore->ctx = ctx;
    semaphore->type = type;
    semaphore->observed  = initial_value;
    semaphore->submitted = initial_value;

    if (type == VK_SEMAPHORE_TYPE_BINARY && !ctx->free_binary_semaphores.empty()) {
        semaphore->semaphore = ctx->free_binary_semaphores.back();
        ctx->free_binary_semaphores.pop_back();
    } else {
        log_debug("Allocating new semaphore of type: {}", magic_enum::enum_name(type));
        wren_check(ctx->vk.CreateSemaphore(ctx->device, wrei_ptr_to(VkSemaphoreCreateInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = wrei_ptr_to(VkSemaphoreTypeCreateInfo {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
                .semaphoreType = type,
                .initialValue = initial_value,
            })
        }), nullptr, &semaphore->semaphore));
    }

    return semaphore;
}

wren_semaphore::~wren_semaphore()
{
    if (type == VK_SEMAPHORE_TYPE_BINARY) {
        ctx->free_binary_semaphores.emplace_back(semaphore);
    } else {
        ctx->vk.DestroySemaphore(ctx->device, semaphore, nullptr);
    }
}

u64 wren_semaphore_advance(wren_semaphore* semaphore, u64 inc)
{
    assert(semaphore->type == VK_SEMAPHORE_TYPE_TIMELINE);

    return semaphore->submitted += inc;
}

u64 wren_semaphore_get_value(wren_semaphore* semaphore)
{
    assert(semaphore->type == VK_SEMAPHORE_TYPE_TIMELINE);

    auto* ctx = semaphore->ctx;

    if (semaphore->submitted > semaphore->observed) {
        wren_check(ctx->vk.GetSemaphoreCounterValue(ctx->device, semaphore->semaphore, &semaphore->observed));
    }

    return semaphore->observed;
}

void wren_semaphore_wait_value(wren_semaphore* semaphore, u64 value)
{
    assert(semaphore->type == VK_SEMAPHORE_TYPE_TIMELINE);

    if (value <= semaphore->observed) return;

    auto* ctx = semaphore->ctx;

    wren_check(ctx->vk.WaitSemaphores(ctx->device, wrei_ptr_to(VkSemaphoreWaitInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores = &semaphore->semaphore,
        .pValues = &value,
    }), UINT64_MAX));

    semaphore->observed = value;
}
