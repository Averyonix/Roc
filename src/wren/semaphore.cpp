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
                .pNext = (type == VK_SEMAPHORE_TYPE_BINARY && ctx->features >= wren_features::dmabuf)
                    ? wrei_ptr_to(VkExportSemaphoreCreateInfo {
                        .sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
                        .handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
                    })
                    : nullptr,
                .semaphoreType = type,
                .initialValue = initial_value,
            })
        }), nullptr, &semaphore->semaphore));
    }

    return semaphore;
}

ref<wren_semaphore> wren_semaphore_import_syncfile(wren_context* ctx, int sync_fd)
{
    assert(ctx->features >= wren_features::dmabuf);

    auto semaphore = wren_semaphore_create(ctx, VK_SEMAPHORE_TYPE_BINARY);
    wren_check(ctx->vk.ImportSemaphoreFdKHR(ctx->device, wrei_ptr_to(VkImportSemaphoreFdInfoKHR {
        .sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,
        .semaphore = semaphore->semaphore,
        .flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT,
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
        .fd = sync_fd,
    })));
    return semaphore;
}

int wren_semaphore_export_syncfile(wren_semaphore* semaphore)
{
    auto* ctx = semaphore->ctx;

    assert(semaphore->type == VK_SEMAPHORE_TYPE_BINARY && ctx->features >= wren_features::dmabuf);

    int sync_fd = -1;
    wren_check(ctx->vk.GetSemaphoreFdKHR(ctx->device, wrei_ptr_to(VkSemaphoreGetFdInfoKHR {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
        .semaphore = semaphore->semaphore,
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
    }), &sync_fd));

    return sync_fd;
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

void wren_semaphore_wait(wren_semaphore* semaphore)
{
    if (semaphore->type == VK_SEMAPHORE_TYPE_TIMELINE) {
        wren_semaphore_wait_value(semaphore, semaphore->submitted);
        return;
    }

    assert(semaphore->ctx->features >= wren_features::dmabuf);

    auto fd = wren_semaphore_export_syncfile(semaphore);

    if (fd == -1) {
        log_error("Failed to export binary semaphore for host waiting");
        return;
    }

    wrei_unix_check_n1(poll(wrei_ptr_to(pollfd {
        .fd = fd,
        .events = EPOLLIN,
    }), 1, -1));

    close(fd);
}
