#include "wren_internal.hpp"

#include "wrei/util.hpp"

const char* wren_result_to_string(VkResult res)
{
    return string_VkResult(res);
}

void wren_wait_for_timeline_value(wren_context* ctx, const VkSemaphoreSubmitInfo& info)
{
    wren_check(ctx->vk.WaitSemaphores(ctx->device, wrei_ptr_to(VkSemaphoreWaitInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores = &info.semaphore,
        .pValues = &info.value,
    }), UINT64_MAX));
}

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

wrei_ref<wren_buffer> wren_buffer_create(wren_context* ctx, usz size)
{
    auto buffer = wrei_adopt_ref(new wren_buffer {});
    buffer->ctx = ctx;

    VmaAllocationInfo vma_alloc_info;
    wren_check(vmaCreateBuffer(ctx->vma, wrei_ptr_to(VkBufferCreateInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT
            | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT
            | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
            | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    }), wrei_ptr_to(VmaAllocationCreateInfo {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
    }), &buffer->buffer, &buffer->vma_allocation, &vma_alloc_info));

    buffer->host_address = vma_alloc_info.pMappedData;

    buffer->device_address = ctx->vk.GetBufferDeviceAddress(ctx->device, wrei_ptr_to(VkBufferDeviceAddressInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = buffer->buffer,
    }));

    return buffer;
}

wren_buffer::~wren_buffer()
{
    vmaDestroyBuffer(ctx->vma, buffer, vma_allocation);
}

// -----------------------------------------------------------------------------

wrei_ref<wren_image> wren_image_create(wren_context* ctx, VkExtent2D extent, VkFormat format)
{
    auto image = wrei_adopt_ref(new wren_image {});
    image->ctx = ctx;

    image->extent = { extent.width, extent.height, 1 };

    wren_check(vmaCreateImage(ctx->vma, wrei_ptr_to(VkImageCreateInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = image->extent,
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    }), wrei_ptr_to(VmaAllocationCreateInfo {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    }), &image->image, &image->vma_allocation, nullptr));

    wren_check(ctx->vk.CreateImageView(ctx->device, wrei_ptr_to(VkImageViewCreateInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    }), nullptr, &image->view));

    wren_allocate_image_descriptor(image.get());

    return image;
}


void wren_image_update(wren_image* image, const void* data)
{
    auto* ctx = image->ctx;
    auto extent = image->extent;

    auto cmd = wren_begin_commands(ctx);

    constexpr auto pixel_size = 4;
    auto row_length = extent.width;
    auto image_height = row_length * extent.height;
    auto image_size = image_height * pixel_size;

    // TODO: This should be stored persistently for transfers
    wrei_ref buffer = wren_buffer_create(ctx, image_size);

    std::memcpy(buffer->host_address, data, image_size);

    wren_transition(ctx, cmd, image->image,
        0, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        0, VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    ctx->vk.CmdCopyBufferToImage(cmd, buffer->buffer, image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, wrei_ptr_to(VkBufferImageCopy {
        .bufferOffset = 0,
        .bufferRowLength = row_length,
        .bufferImageHeight = image_size,
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageOffset = {},
        .imageExtent = { extent.width, extent.height, 1 },
    }));

    wren_transition(ctx, cmd, image->image,
        0, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        0, VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);

    wren_submit_commands(ctx, cmd);
}

wren_image::~wren_image()
{
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

wrei_ref<wren_sampler> wren_sampler_create(wren_context* ctx)
{
    wrei_ref sampler = wrei_adopt_ref(new wren_sampler {});
    sampler->ctx = ctx;

    wren_check(ctx->vk.CreateSampler(ctx->device, wrei_ptr_to(VkSamplerCreateInfo {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
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
    ctx->sampler_descriptor_allocator.free(id);

    ctx->vk.DestroySampler(ctx->device, sampler, nullptr);
}
