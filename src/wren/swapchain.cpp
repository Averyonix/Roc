#include "internal.hpp"

wren_swapchain::~wren_swapchain()
{
    vkwsi_swapchain_destroy(swapchain);
}

ref<wren_swapchain> wren_swapchain_create(wren_context* ctx, VkSurfaceKHR surface, wren_format format)
{
    auto swapchain = wrei_create<wren_swapchain>();
    swapchain->ctx = ctx;
    swapchain->surface = surface;
    swapchain->format = format;

    log_debug("Creating vulkan swapchain");
    wren_check(vkwsi_swapchain_create(&swapchain->swapchain, ctx->vkwsi, surface));

    std::vector<VkSurfaceFormatKHR> surface_formats;
    wren_vk_enumerate(surface_formats, ctx->vk.GetPhysicalDeviceSurfaceFormatsKHR, ctx->physical_device, surface);
    bool found = false;
    for (auto& f : surface_formats) {
        // TODO: We need to handle selection of format between outputs and render pipelines
        // if (f.format == VK_FORMAT_R8G8B8A8_SRGB || f.format == VK_FORMAT_B8G8R8A8_SRGB) {
        // if (f.format == VK_FORMAT_R8G8B8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_UNORM) {
        if (f.format == format->vk) {
            swapchain->color_space = f.colorSpace;
            found = true;
            break;
        }
    }
    if (!found) {
        log_error("Could not find appropriate swapchain format");
        return nullptr;
    }

    auto sw_info = vkwsi_swapchain_info_default();
    sw_info.image_sharing_mode = VK_SHARING_MODE_EXCLUSIVE;
    sw_info.image_usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
        // TODO: DONT DO THIS - Use an intermediary buffer or multi-view rendering for screenshots
        | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    sw_info.present_mode = VK_PRESENT_MODE_FIFO_KHR;
    sw_info.min_image_count = 2;
    sw_info.format = format->vk;
    sw_info.color_space = swapchain->color_space;
    vkwsi_swapchain_set_info(swapchain->swapchain, &sw_info);

    swapchain->current = wrei_create<wren_image>();
    swapchain->current->ctx = ctx;
    swapchain->current->format = format;

    return swapchain;
}

void wren_swapchain_resize(wren_swapchain* swapchain, vec2u32 extent)
{
    wren_check(vkwsi_swapchain_resize(swapchain->swapchain, {extent.x, extent.y}));
}

wren_image* wren_swapchain_acquire_image(wren_swapchain* swapchain, std::span<const wren_syncpoint> signals)
{
    assert(std::ranges::all_of(signals, [](auto& s) { return s.semaphore->type == VK_SEMAPHORE_TYPE_BINARY; }));

    auto signal_infos = wren_syncpoints_to_submit_infos(signals);

    wren_check(vkwsi_swapchain_acquire(&swapchain->swapchain, 1, swapchain->ctx->queue, signal_infos.data(), signal_infos.size()));

    auto current = vkwsi_swapchain_get_current(swapchain->swapchain);
    swapchain->current->image = current.image;
    swapchain->current->view = current.view;
    swapchain->current->extent = {current.extent.width, current.extent.height};

    if (swapchain->resources.size() < current.index + 1) {
        swapchain->resources.resize(current.index + 1);
    }
    swapchain->resources[current.index] = {};

    return swapchain->current.get();
}

void wren_swapchain_present(wren_swapchain* swapchain, std::span<const wren_syncpoint> waits)
{
    assert(std::ranges::all_of(waits, [](auto& s) { return s.semaphore->type == VK_SEMAPHORE_TYPE_BINARY; }));

    auto wait_infos = wren_syncpoints_to_submit_infos(waits);

    auto current = vkwsi_swapchain_get_current(swapchain->swapchain);
    for (auto& w : waits) {
        swapchain->resources[current.index].objects.emplace_back(w.semaphore);
    }

    wren_check(vkwsi_swapchain_present(&swapchain->swapchain, 1, swapchain->ctx->queue, wait_infos.data(), wait_infos.size(), false));
}

VkSemaphoreSubmitInfo wren_syncpoint_to_submit_info(const wren_syncpoint& syncpoint)
{
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
