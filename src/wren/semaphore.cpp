#include "internal.hpp"

VkSemaphoreSubmitInfo wren_syncpoint_to_submit_info(const wren_syncpoint& syncpoint)
{
    return {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = syncpoint.semaphore->semaphore,
        .value     = syncpoint.value,
        .stageMask = syncpoint.stages,
    };
}

ref<wren_semaphore> wren_semaphore_create(wren_context* ctx, u64 initial_value)
{
    auto semaphore = wrei_create<wren_semaphore>();
    semaphore->ctx = ctx;

    // Here we are creating a timeline sempahore, and exporting a persistent syncboj
    // handle to it that we can use for importing/exporting syncobj files for interop.
    // This trick only works when the driver uses syncobjs as the opaque fd type.
    // This seems to work fine on AMD, but definitely won't work for all vendors.

    // As a portable solution, we'll have to use DRM syncobjs as the underlying type, creating
    // binary semaphores as required for vulkan waits

    wren_check(ctx->vk.CreateSemaphore(ctx->device, wrei_ptr_to(VkSemaphoreCreateInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = wrei_ptr_to(VkSemaphoreTypeCreateInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
            .pNext = wrei_ptr_to(VkExportSemaphoreCreateInfo {
                .sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
                .handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT,
            }),
            .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
            .initialValue = initial_value,
        })
    }), nullptr, &semaphore->semaphore));

    int syncobj_fd = -1;
    wren_check(ctx->vk.GetSemaphoreFdKHR(ctx->device, wrei_ptr_to(VkSemaphoreGetFdInfoKHR {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
        .semaphore = semaphore->semaphore,
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT,
    }), &syncobj_fd));

    wrei_unix_check_n1(drmSyncobjFDToHandle(ctx->drm_fd, syncobj_fd, &semaphore->syncobj));

    return semaphore;
}

ref<wren_semaphore> wren_semaphore_import_syncobj(wren_context* ctx, int syncobj_fd)
{
    auto semaphore = wren_semaphore_create(ctx);
    wren_check(ctx->vk.ImportSemaphoreFdKHR(ctx->device, wrei_ptr_to(VkImportSemaphoreFdInfoKHR {
        .sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,
        .semaphore = semaphore->semaphore,
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT,
        .fd = syncobj_fd,
    })));

    return semaphore;
}

void wren_semaphore_import_syncfile(wren_semaphore* semaphore, int sync_fd, u64 target_point)
{
    auto* ctx = semaphore->ctx;

    // We can't import from a syncfile directly to a syncboj point, so we import to a non-timeline syncobj first
    // and then transfer to our target point from that.

    u32 syncobj = {};
    wrei_unix_check_n1(drmSyncobjCreate(ctx->drm_fd, 0, &syncobj));
    defer { wrei_unix_check_n1(drmSyncobjDestroy(ctx->drm_fd, syncobj)); };
    wrei_unix_check_n1(drmSyncobjImportSyncFile(ctx->drm_fd, syncobj, sync_fd));
    wrei_unix_check_n1(drmSyncobjTransfer(ctx->drm_fd, semaphore->syncobj, target_point, syncobj, 0, 0));
}

int wren_semaphore_export_syncfile(wren_semaphore* semaphore, u64 source_point)
{
    auto* ctx = semaphore->ctx;

    // We can't export directly from a timeline syncboj to a syncfile, so we transfer to a non-timeline syncboj first
    // and then export the syncfile from that.

    u32 syncobj = {};
    wrei_unix_check_n1(drmSyncobjCreate(ctx->drm_fd, 0, &syncobj));
    defer { wrei_unix_check_n1(drmSyncobjDestroy(ctx->drm_fd, syncobj)); };
    wrei_unix_check_n1(drmSyncobjTransfer(ctx->drm_fd, syncobj, 0, semaphore->syncobj, source_point, 0));
    int sync_fd = -1;
    wrei_unix_check_n1(drmSyncobjExportSyncFile(ctx->drm_fd, syncobj, &sync_fd));

    return sync_fd;
}

wren_semaphore::~wren_semaphore()
{
    ctx->vk.DestroySemaphore(ctx->device, semaphore, nullptr);
    wrei_unix_check_n1(drmSyncobjDestroy(ctx->drm_fd, syncobj));
}

#define WREN_SEMAPHORE_PREFER_VK_OPS 1

u64 wren_semaphore_get_value(wren_semaphore* semaphore)
{
    auto* ctx = semaphore->ctx;

    u64 value;
#if WREN_SEMAPHORE_PREFER_VK_OPS
    wren_check(ctx->vk.GetSemaphoreCounterValue(ctx->device, semaphore->semaphore, &value));
#else
    wrei_unix_check_n1(drmSyncobjQuery2(ctx->drm_fd, &semaphore->syncobj, &value, 1, 0));
#endif

    return value;
}

void wren_semaphore_wait_value(wren_semaphore* semaphore, u64 value)
{
    auto* ctx = semaphore->ctx;

#if WREN_SEMAPHORE_PREFER_VK_OPS
    wren_check(ctx->vk.WaitSemaphores(ctx->device, wrei_ptr_to(VkSemaphoreWaitInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores = &semaphore->semaphore,
        .pValues = &value,
    }), UINT64_MAX));
#else
    u32 first_signalled;
    wrei_unix_check_n1(drmSyncobjTimelineWait(ctx->drm_fd,
        &semaphore->syncobj, &value, 1, INT64_MAX, 0, &first_signalled));
#endif
}

void wren_semaphore_signal_value(wren_semaphore* semaphore, u64 value)
{
    auto* ctx = semaphore->ctx;

#if WREN_SEMAPHORE_PREFER_VK_OPS
    wren_check(ctx->vk.SignalSemaphore(ctx->device, wrei_ptr_to(VkSemaphoreSignalInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,
        .semaphore = semaphore->semaphore,
        .value = value,
    })));
#else
    wrei_unix_check_n1(drmSyncobjTimelineSignal(ctx->drm_fd, &semaphore->syncobj, &value, 1));
#endif
}

// -----------------------------------------------------------------------------

ref<wren_binary_semaphore> wren_binary_semaphore_create(wren_context* ctx)
{
    auto binary = wrei_create<wren_binary_semaphore>();
    binary->ctx = ctx;

    if (!ctx->free_binary_semaphores.empty()) {
        binary->semaphore = ctx->free_binary_semaphores.back();
        ctx->free_binary_semaphores.pop_back();
    } else {
        wren_check(ctx->vk.CreateSemaphore(ctx->device, wrei_ptr_to(VkSemaphoreCreateInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        }), nullptr, &binary->semaphore));
    }

    return binary;
}

ref<wren_binary_semaphore> wren_semaphore_transfer_to_binary(wren_semaphore* semaphore, u64 source_point)
{
    auto sync_fd = wren_semaphore_export_syncfile(semaphore, source_point);

    auto* ctx = semaphore->ctx;

    auto binary = wren_binary_semaphore_create(ctx);

    wren_check(ctx->vk.ImportSemaphoreFdKHR(ctx->device, wrei_ptr_to(VkImportSemaphoreFdInfoKHR {
        .sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,
        .semaphore = binary->semaphore,
        .flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT,
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
        .fd = sync_fd,
    })));

    return binary;
}

wren_binary_semaphore::~wren_binary_semaphore()
{
    ctx->free_binary_semaphores.emplace_back(semaphore);
}
