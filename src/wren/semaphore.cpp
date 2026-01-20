#include "internal.hpp"

VkSemaphoreSubmitInfo wren_syncpoint_to_submit_info(const wren_syncpoint& syncpoint)
{
    assert(syncpoint.semaphore->type != wren_semaphore_type::syncobj && "TODO");

    return {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = syncpoint.semaphore->semaphore,
        .value     = syncpoint.value,
        .stageMask = syncpoint.stages,
    };
}

std::vector<VkSemaphoreSubmitInfo> wren_syncpoints_to_submit_infos(std::span<const wren_syncpoint> syncpoints, const wren_syncpoint* extra)
{
    std::vector<VkSemaphoreSubmitInfo> infos(syncpoints.size() + usz(bool(extra)));

    u32 i = 0;
    for (auto& s : syncpoints) {
        infos[i++] = wren_syncpoint_to_submit_info(s);
    }
    if (extra) {
        infos[i] = wren_syncpoint_to_submit_info(*extra);
    }

    return infos;
}

ref<wren_semaphore> wren_semaphore_create(wren_context* ctx, wren_semaphore_type type, u64 initial_value)
{
    auto semaphore = wrei_create<wren_semaphore>();
    semaphore->ctx = ctx;
    semaphore->type = type;

    if (type == wren_semaphore_type::binary && !ctx->free_binary_semaphores.empty()) {
        semaphore->semaphore = ctx->free_binary_semaphores.back();
        ctx->free_binary_semaphores.pop_back();
        return semaphore;
    } else {
        log_debug("Allocating new semaphore of type: {}", wrei_enum_to_string(type));
        if (type == wren_semaphore_type::syncobj) {
            wrei_unix_check_n1(drmSyncobjCreate(ctx->drm_fd, 0, &semaphore->syncobj));
            if (initial_value) {
                wren_semaphore_signal_value(semaphore.get(), initial_value);
            }
            return semaphore;
        }
        wren_check(ctx->vk.CreateSemaphore(ctx->device, wrei_ptr_to(VkSemaphoreCreateInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = wrei_ptr_to(VkSemaphoreTypeCreateInfo {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
                .pNext = (type == wren_semaphore_type::binary && ctx->features >= wren_features::dmabuf)
                    ? wrei_ptr_to(VkExportSemaphoreCreateInfo {
                        .sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
                        .handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
                    })
                    : nullptr,
                .semaphoreType = type == wren_semaphore_type::binary
                    ? VK_SEMAPHORE_TYPE_BINARY
                    : VK_SEMAPHORE_TYPE_TIMELINE,
                .initialValue = initial_value,
            })
        }), nullptr, &semaphore->semaphore));
        return semaphore;
    }
}

ref<wren_semaphore> wren_semaphore_import_syncobj(wren_context* ctx, int syncobj_fd)
{
    assert(ctx->features >= wren_features::dmabuf);

#if WREN_IMPORT_SYNCOBJ_AS_TIMELINE
    // This trick only works when the driver uses syncobjs as the opaque fd type.
    // This seems to work fine on AMD, but likely won't work for all vendors.
    // We also can't assume that vkImportSemaphoreFdKHR will report an error when
    // passing an invalid opaque fd, as it's invalid API usage, this is best left
    // as a flag that must be explicitly enabled.

    auto semaphore = wren_semaphore_create(ctx, VK_SEMAPHORE_TYPE_TIMELINE);
    wren_check(ctx->vk.ImportSemaphoreFdKHR(ctx->device, wrei_ptr_to(VkImportSemaphoreFdInfoKHR {
        .sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,
        .semaphore = semaphore->semaphore,
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT,
        .fd = syncobj_fd,
    })));

    return semaphore;
#else
    auto semaphore = wrei_create<wren_semaphore>();
    semaphore->ctx = ctx;
    semaphore->type = wren_semaphore_type::syncobj;

    wrei_unix_check_n1(drmSyncobjFDToHandle(ctx->drm_fd, syncobj_fd, &semaphore->syncobj));
    close(syncobj_fd);

    return semaphore;
#endif
}

ref<wren_semaphore> wren_semaphore_import_syncfile(wren_context* ctx, int sync_fd)
{
    assert(ctx->features >= wren_features::dmabuf);

    auto semaphore = wren_semaphore_create(ctx, wren_semaphore_type::binary);
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

    assert(semaphore->type == wren_semaphore_type::binary && ctx->features >= wren_features::dmabuf);

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
    switch (type) {
        break;case wren_semaphore_type::binary:
            // TODO: Ensure semaphore has been waited on before re-using
            ctx->free_binary_semaphores.emplace_back(semaphore);
        break;case wren_semaphore_type::timeline:
            ctx->vk.DestroySemaphore(ctx->device, semaphore, nullptr);
        break;case wren_semaphore_type::syncobj:
            wrei_unix_check_n1(drmSyncobjDestroy(ctx->drm_fd, syncobj));
    }
}

u64 wren_semaphore_get_value(wren_semaphore* semaphore)
{
    auto* ctx = semaphore->ctx;

    u64 value;

    switch (semaphore->type) {
        break;case wren_semaphore_type::timeline:
            wren_check(ctx->vk.GetSemaphoreCounterValue(ctx->device, semaphore->semaphore, &value));
        break;case wren_semaphore_type::syncobj:
            wrei_unix_check_n1(drmSyncobjQuery2(ctx->drm_fd, &semaphore->syncobj, &value, 1, 0));
        break;default:
            std::unreachable();
    }

    return value;
}

void wren_semaphore_wait_value(wren_semaphore* semaphore, u64 value)
{
    auto* ctx = semaphore->ctx;

    switch (semaphore->type) {
        break;case wren_semaphore_type::timeline:
            wren_check(ctx->vk.WaitSemaphores(ctx->device, wrei_ptr_to(VkSemaphoreWaitInfo {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
                .semaphoreCount = 1,
                .pSemaphores = &semaphore->semaphore,
                .pValues = &value,
            }), UINT64_MAX));
        break;case wren_semaphore_type::syncobj: {
            u32 first_signalled;
            wrei_unix_check_n1(drmSyncobjTimelineWait(ctx->drm_fd,
                &semaphore->syncobj, &value, 1, INT64_MAX, 0, &first_signalled));
        }
        break;default:
            std::unreachable();
    }

}

void wren_semaphore_signal_value(wren_semaphore* semaphore, u64 value)
{
    auto* ctx = semaphore->ctx;

    switch (semaphore->type) {
        break;case wren_semaphore_type::timeline:
            wren_check(ctx->vk.SignalSemaphore(ctx->device, wrei_ptr_to(VkSemaphoreSignalInfo {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,
                .semaphore = semaphore->semaphore,
                .value = value,
            })));
        break;case wren_semaphore_type::syncobj:
            wrei_unix_check_n1(drmSyncobjTimelineSignal(ctx->drm_fd, &semaphore->syncobj, &value, 1));
        break;default:
            std::unreachable();
    }

}
