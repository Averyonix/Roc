#include "internal.hpp"

#include "wrei/util.hpp"

void wren_transition(wren_context* ctx, VkCommandBuffer cmd, VkImage image,
    VkPipelineStageFlags2 src, VkPipelineStageFlags2 dst,
    VkAccessFlags2 src_access, VkAccessFlags2 dst_access,
    VkImageLayout old_layout, VkImageLayout new_layout)
{
    ctx->vk.CmdPipelineBarrier2(cmd, wrei_ptr_to(VkDependencyInfo {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = wrei_ptr_to(VkImageMemoryBarrier2 {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = src,
            .srcAccessMask = src_access,
            .dstStageMask = dst,
            .dstAccessMask = dst_access,
            .oldLayout = old_layout,
            .newLayout = new_layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        }),
    }));
}

// -----------------------------------------------------------------------------

ref<wren_image> wren_image_create(wren_context* ctx, vec2u32 extent, wren_format format)
{
    auto image = wrei_create<wren_image>();
    image->ctx = ctx;

    ctx->stats.active_images++;

    image->extent = extent;
    image->format = format;

    VmaAllocationInfo alloc_info;
    wren_check(vmaCreateImage(ctx->vma, wrei_ptr_to(VkImageCreateInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format->vk,
        .extent = {extent.x, extent.y, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT
               | VK_IMAGE_USAGE_TRANSFER_DST_BIT
               | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    }), wrei_ptr_to(VmaAllocationCreateInfo {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    }), &image->image, &image->vma_allocation, &alloc_info));

    image->stats.owned_allocation_size += alloc_info.size;
    ctx->stats.active_image_owned_memory += alloc_info.size;

    wren_image_init(image.get());

    return image;
}

void wren_image_init(wren_image* image)
{
    auto* ctx = image->ctx.get();

    wren_check(ctx->vk.CreateImageView(ctx->device, wrei_ptr_to(VkImageViewCreateInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = image->format->vk,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    }), nullptr, &image->view));

    wren_allocate_image_descriptor(image);

    auto cmd = wren_begin_commands(ctx);
    wren_transition(ctx, cmd, image->image,
        0, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        0, VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    wren_submit_commands(ctx, cmd);
}

void wren_image_readback(wren_image* image, void* data)
{
    auto* ctx = image->ctx.get();
    auto extent = image->extent;

    auto cmd = wren_begin_commands(ctx);

    constexpr auto pixel_size = 4;
    auto row_length = extent.x;
    auto image_height = row_length * extent.y;
    auto image_size = image_height * pixel_size;

    // TODO: This should be stored persistently for transfers
    ref buffer = wren_buffer_create(ctx, image_size);

    ctx->vk.CmdCopyImageToBuffer(cmd, image->image, VK_IMAGE_LAYOUT_GENERAL, buffer->buffer, 1, wrei_ptr_to(VkBufferImageCopy {
        .bufferOffset = 0,
        .bufferRowLength = row_length,
        .bufferImageHeight = image_size,
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageOffset = {},
        .imageExtent = { extent.x, extent.y, 1 },
    }));

    wren_submit_commands(ctx, cmd);

    std::memcpy(data, buffer->host_address, image_size);
}

void wren_image_update(wren_image* image, const void* data)
{
    auto* ctx = image->ctx.get();
    auto extent = image->extent;

    auto cmd = wren_begin_commands(ctx);

    constexpr auto pixel_size = 4;
    auto row_length = extent.x;
    auto image_height = row_length * extent.y;
    auto image_size = image_height * pixel_size;

    // TODO: This should be stored persistently for transfers
    ref buffer = wren_buffer_create(ctx, image_size);

    std::memcpy(buffer->host_address, data, image_size);

    ctx->vk.CmdCopyBufferToImage(cmd, buffer->buffer, image->image, VK_IMAGE_LAYOUT_GENERAL, 1, wrei_ptr_to(VkBufferImageCopy {
        .bufferOffset = 0,
        .bufferRowLength = row_length,
        .bufferImageHeight = image_size,
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageOffset = {},
        .imageExtent = { extent.x, extent.y, 1 },
    }));

    wren_submit_commands(ctx, cmd);
}

wren_image::~wren_image()
{
    // TODO: Proper non-owning image support
    if (!id) {
        log_debug("Image isn't imported, don't free");
        return;
    }

    ctx->stats.active_images--;
    ctx->stats.active_image_owned_memory -= stats.owned_allocation_size;
    ctx->stats.active_image_imported_memory -= stats.imported_allocation_size;

    log_debug("wren_image::destroyed");

    ctx->image_descriptor_allocator.free(id);

    ctx->vk.DestroyImageView(ctx->device, view, nullptr);

    if (vma_allocation) {
        vmaDestroyImage(ctx->vma, image, vma_allocation);
    } else {
        ctx->vk.DestroyImage(ctx->device, image, nullptr);
        ctx->vk.FreeMemory(ctx->device, memory, nullptr);
    }
}

// -----------------------------------------------------------------------------

ref<wren_sampler> wren_sampler_create(wren_context* ctx, VkFilter mag, VkFilter min)
{
    ref sampler = wrei_create<wren_sampler>();
    sampler->ctx = ctx;

    ctx->stats.active_samplers++;

    wren_check(ctx->vk.CreateSampler(ctx->device, wrei_ptr_to(VkSamplerCreateInfo {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = mag,
        .minFilter = min,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .anisotropyEnable = false,
        .maxLod = VK_LOD_CLAMP_NONE,
        .borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
    }), nullptr, &sampler->sampler));

    wren_allocate_sampler_descriptor(sampler.get());

    return sampler;
}

wren_sampler::~wren_sampler()
{
    ctx->stats.active_samplers--;

    ctx->sampler_descriptor_allocator.free(id);

    ctx->vk.DestroySampler(ctx->device, sampler, nullptr);
}

// -----------------------------------------------------------------------------

u32 wren_find_vk_memory_type_index(wren_context* ctx, u32 type_filter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties props;
    ctx->vk.GetPhysicalDeviceMemoryProperties(ctx->physical_device, &props);

    for (u32 i = 0; i < props.memoryTypeCount; ++i) {
        std::string flags;
        if (!(type_filter & (1 << i))) continue;
        if ((props.memoryTypes[i].propertyFlags & properties) != properties) continue;

        return i;
    }

    log_error("Failed to find suitable memory type");
    return 0xFF;
}

ref<wren_image> wren_image_import_dmabuf(wren_context* ctx, const wren_dma_params& params)
{
    auto image = wrei_create<wren_image>();
    image->ctx = ctx;

    ctx->stats.active_images++;

    image->extent = params.extent;
    image->format = params.format;

    VkExternalMemoryHandleTypeFlagBits htype = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    VkImageCreateInfo img_info {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = params.format->vk,
        .extent = {image->extent.x, image->extent.y, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .usage = wren_dma_texture_usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VkExternalMemoryImageCreateInfo eimg = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
    };
    img_info.pNext = &eimg;

    VkSubresourceLayout plane_layouts[wren_dma_max_planes] = {};

    img_info.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
    for (u32 i = 0; i < params.planes.size(); ++i) {
        plane_layouts[i].offset = params.planes[i].offset;
        plane_layouts[i].rowPitch = params.planes[i].stride;
    }

    VkImageDrmFormatModifierExplicitCreateInfoEXT mod_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT,
        .drmFormatModifier = params.planes.front().drm_modifier,
        .drmFormatModifierPlaneCount = u32(params.planes.size()),
        .pPlaneLayouts = plane_layouts,
    };
    eimg.pNext = &mod_info;

    wren_check(ctx->vk.CreateImage(ctx->device, &img_info, nullptr, &image->image));

    image->dma_params = std::make_unique<wren_dma_params>(params);

    VkBindImageMemoryInfo bindi = {};

    {
        VkMemoryFdPropertiesKHR fdp = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR,
        };
        log_trace("  num_planes = {}", params.planes.size());
        log_trace("  plane[0].fd = {}", params.planes.front().fd);
        auto mod_name = drmGetFormatModifierName(params.planes.front().drm_modifier);
        defer { free(mod_name); };
        log_trace("  plane[0].modifier = {}", mod_name);
        log_trace("  ctx->vk.GetMemoryFdPropertiesKHR = {}", (void*)ctx->vk.GetMemoryFdPropertiesKHR);
        wren_check(ctx->vk.GetMemoryFdPropertiesKHR(ctx->device, htype, params.planes.front().fd, &fdp));

        // TODO: Multi-plane support
        assert(params.planes.size() == 1);

        VkImageMemoryRequirementsInfo2 memri = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
            .image = image->image,
        };

        VkMemoryRequirements2 memr = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
        };

        ctx->vk.GetImageMemoryRequirements2(ctx->device, &memri, &memr);

        auto mem = wren_find_vk_memory_type_index(ctx, memr.memoryRequirements.memoryTypeBits & fdp.memoryTypeBits, 0);

        // Take a copy of the file descriptor, this will be owned by the bound vulkan memory
        int dfd = fcntl(params.planes.front().fd, F_DUPFD_CLOEXEC, 0);
        image->dma_params->planes[0].fd = dfd;

        VkMemoryAllocateInfo memi = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = memr.memoryRequirements.size,
            .memoryTypeIndex = mem,
        };

        image->stats.imported_allocation_size += memr.memoryRequirements.size;
        ctx->stats.active_image_imported_memory += memr.memoryRequirements.size;

        VkImportMemoryFdInfoKHR importi = {
            .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
            .handleType = htype,
            .fd = dfd,
        };
        memi.pNext = &importi;

        VkMemoryDedicatedAllocateInfo dedi = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
            .image = image->image,
        };
        importi.pNext = &dedi;

        wren_check(ctx->vk.AllocateMemory(ctx->device, &memi, nullptr, &image->memory));

        bindi.image = image->image;
        bindi.memory = image->memory;
        bindi.memoryOffset = 0;
        bindi.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
    }

    wren_check(ctx->vk.BindImageMemory2(ctx->device, 1, &bindi));

    wren_image_init(image.get());

    return image;
}

void wren_image_wait(wren_image* image)
{
    auto* params = image->dma_params.get();
    if (!params) return;

    // TODO: Importing sync files to Vulkan semaphores
    // TODO: Wait for images asynchronously to prevent frames from being delayed

    for (auto& plane : params->planes) {
        pollfd pfd {
            .fd = plane.fd,
            .events = POLLIN,
        };
        int timeout_ms = 1000;
        auto start = std::chrono::steady_clock::now();
        int ret = poll(&pfd, 1, timeout_ms);
            auto dur = std::chrono::steady_clock::now() - start;
        if (ret < 0) {
            log_error("Failed to wait for DMA-BUF");
        } else if (ret == 0) {
            log_error("Timed out waiting for DMA-BUF fence after {}", wrei_duration_to_string(dur));
        } else {
            if (dur > 1ms) {
                log_warn("Waiting for imported DMA-BUF took a long time: {}", wrei_duration_to_string(dur));
            }
        }
    }
}
