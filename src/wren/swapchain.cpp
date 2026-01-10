#include "internal.hpp"

wren_image_swapchain::~wren_image_swapchain()
{
    ctx->vk.DestroyImageView(ctx->device, view, nullptr);
}

static
void request_acquire(wren_swapchain* swapchain)
{
    swapchain->acquire_semaphore = wren_semaphore_create(swapchain->ctx, VK_SEMAPHORE_TYPE_BINARY);
    swapchain->current_index = wren_swapchain::invalid_index;

    swapchain->can_acquire = true;
    swapchain->can_acquire.notify_one();
}

static
void acquire_thread(weak<wren_swapchain> swapchain)
{
    auto* ctx = swapchain->ctx;
    for (;;) {
        swapchain->can_acquire.wait(false);

        if (swapchain->destroy_requested) {
            log_debug("Swapchain destroy requested before acquire, closing...");
            return;
        }

        u32 image_index = wren_swapchain::invalid_index;
        wren_check(ctx->vk.AcquireNextImageKHR(ctx->device, swapchain->swapchain, UINT64_MAX, swapchain->acquire_semaphore->semaphore, nullptr, &image_index));

        if (swapchain->destroy_requested) {
            log_debug("Swapchain destroy requested after acquire, closing...");
            swapchain->current_index = image_index;
            return;
        }

        swapchain->can_acquire = false;
        swapchain->can_acquire.notify_one();

        wrei_event_loop_enqueue(ctx->event_loop.get(), [swapchain, image_index] {
            if (swapchain) {
                swapchain->current_index = image_index;
                swapchain->acquire_ready = true;
                if (swapchain->acquire_callback) {
                    swapchain->acquire_callback();
                }
            }
        });
    }
}

wren_swapchain::~wren_swapchain()
{
    log_debug("Swapchain being destroyed");

    destroy_requested = true;
    can_acquire = true;
    can_acquire.notify_one();

    acquire_thread.join();

    log_debug("Acquire thread joined");

    // TODO: Present VkFence
    wren_wait_idle(wren_get_queue(ctx, wren_queue_type::graphics));

    if (current_index != invalid_index) {
        log_debug("Releasing swapchain images");
        ctx->vk.ReleaseSwapchainImagesEXT(ctx->device, wrei_ptr_to(VkReleaseSwapchainImagesInfoEXT {
            .sType = VK_STRUCTURE_TYPE_RELEASE_SWAPCHAIN_IMAGES_INFO_EXT,
            .swapchain = swapchain,
            .imageIndexCount = 1,
            .pImageIndices = &current_index,
        }));
    }

    images.clear();
    ctx->vk.DestroySwapchainKHR(ctx->device, swapchain, nullptr);
}

static
void swapchain_recreate(wren_swapchain* swapchain)
{
    auto* ctx = swapchain->ctx;

    // TODO: Better sync
    wren_wait_idle(wren_get_queue(ctx, wren_queue_type::graphics));

    u32 min_image_count = 4;
    // VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_MAILBOX_KHR;

    VkExtent2D extent { swapchain->pending.extent.x, swapchain->pending.extent.y };

    auto old_swapchain = swapchain->swapchain;
    wren_check(ctx->vk.CreateSwapchainKHR(ctx->device, wrei_ptr_to(VkSwapchainCreateInfoKHR {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = wrei_ptr_to(VkSwapchainPresentModesCreateInfoEXT {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODES_CREATE_INFO_EXT,
            .presentModeCount = 1,
            .pPresentModes = &present_mode,
        }),
        .surface = swapchain->surface,
        .minImageCount = min_image_count,
        .imageFormat = swapchain->format->vk,
        .imageColorSpace = swapchain->color_space,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        // TODO: wren_image_usage
        .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = present_mode,
        .oldSwapchain = old_swapchain,
    }), nullptr, &swapchain->swapchain));

    swapchain->extent = swapchain->pending.extent;

    ctx->vk.DestroySwapchainKHR(ctx->device, old_swapchain, nullptr);

    std::vector<VkImage> images;
    wren_vk_enumerate(images, ctx->vk.GetSwapchainImagesKHR, ctx->device, swapchain->swapchain);

    swapchain->resources.clear();
    swapchain->resources.resize(images.size());

    swapchain->images.clear();
    for (auto& vk_image : images) {
        auto image = swapchain->images.emplace_back(wrei_create<wren_image_swapchain>()).get();
        image->ctx = ctx;
        image->image = vk_image;
        image->extent = vec2u32(extent.width, extent.height);
        image->format = swapchain->format;

        wren_check(ctx->vk.CreateImageView(ctx->device, wrei_ptr_to(VkImageViewCreateInfo {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image->image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = image->format->vk,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        }), nullptr, &image->view));
    }
}

ref<wren_swapchain> wren_swapchain_create(wren_context* ctx, VkSurfaceKHR surface, wren_format format)
{
    ref swapchain = wrei_create<wren_swapchain>();
    swapchain->ctx = ctx;
    swapchain->format = format;
    swapchain->surface = surface;

    std::vector<VkSurfaceFormatKHR> surface_formats;
    wren_vk_enumerate(surface_formats, ctx->vk.GetPhysicalDeviceSurfaceFormatsKHR, ctx->physical_device, surface);
    bool found = false;
    for (auto& f : surface_formats) {
        if (f.format == format->vk) {
            swapchain->color_space = f.colorSpace;
            found = true;
            break;
        }
    }
    if (!found) {
        log_error("Could not find appropriate swapchain foramt");
        return nullptr;
    }

    return swapchain;
}

void wren_swapchain_resize(wren_swapchain* swapchain, vec2u32 extent)
{
    swapchain->pending.extent = extent;
    if (!swapchain->swapchain) {
        swapchain_recreate(swapchain);

        swapchain->acquire_thread = std::jthread{acquire_thread, swapchain};
        request_acquire(swapchain);
    }
}

std::pair<wren_image*, wren_syncpoint> wren_swapchain_acquire_image(wren_swapchain* swapchain)
{
    assert(swapchain->swapchain);

    if (!swapchain->acquire_ready) {
        return {};
    }

    swapchain->acquire_ready = false;

    swapchain->resources[swapchain->current_index] = {};

    assert(swapchain->current_index != wren_swapchain::invalid_index);

    return {
        swapchain->images[swapchain->current_index].get(),
        {swapchain->acquire_semaphore.get()}
    };
}

void wren_swapchain_present(wren_swapchain* swapchain, std::span<const wren_syncpoint> waits)
{
    assert(std::ranges::all_of(waits, [](auto& s) { return s.semaphore->type == VK_SEMAPHORE_TYPE_BINARY; }));

    std::vector<VkSemaphore> semas(waits.size());
    for (u32 i = 0; i < waits.size(); ++i) {
        semas[i] = waits[i].semaphore->semaphore;
        swapchain->resources[swapchain->current_index].objects.emplace_back(waits[i].semaphore);
    }

    auto* ctx = swapchain->ctx;

    // TODO: Asynchronous present?
    auto queue = wren_get_queue(ctx, wren_queue_type::graphics);
    wren_check(ctx->vk.QueuePresentKHR(queue->queue, wrei_ptr_to(VkPresentInfoKHR {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = u32(semas.size()),
        .pWaitSemaphores = semas.data(),
        .swapchainCount = 1,
        .pSwapchains = &swapchain->swapchain,
        .pImageIndices = &swapchain->current_index,
    })));

    if (swapchain->pending.extent != swapchain->extent) {
        swapchain_recreate(swapchain);
    }

    request_acquire(swapchain);
}
