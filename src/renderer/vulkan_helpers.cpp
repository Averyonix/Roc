#include "vulkan_helpers.hpp"

#include "vulkan_context.hpp"

#include "common/util.hpp"

#include "vulkan/vk_enum_string_helper.h"

const char* vk_result_to_string(VkResult res)
{
    return string_VkResult(res);
}

void vk_transition(VulkanContext* vk, VkCommandBuffer cmd, VkImage image,
    VkPipelineStageFlags2 src, VkPipelineStageFlags2 dst,
    VkAccessFlags2 src_access, VkAccessFlags2 dst_access,
    VkImageLayout old_layout, VkImageLayout new_layout)
{
    vk->CmdPipelineBarrier2(cmd, ptr_to(VkDependencyInfo {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = ptr_to(VkImageMemoryBarrier2 {
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

u32 vk_find_memory_type(VulkanContext* vk, u32 type_filter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties props;
    vk->GetPhysicalDeviceMemoryProperties(vk->physical_device, &props);

    for (u32 i = 0; i < props.memoryTypeCount; ++i) {
        std::string flags;
        if (!(type_filter & (1 << i))) continue;
        if ((props.memoryTypes[i].propertyFlags & properties) != properties) continue;

        return i;
    }

    log_error("Failed to find suitable memory type");
    return 0xFF;
}

VulkanBuffer vk_buffer_create(VulkanContext* vk, usz size)
{
    VulkanBuffer buffer {};

    vk_check(vk->CreateBuffer(vk->device, ptr_to(VkBufferCreateInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT
            | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT
            | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
            | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        // .sharingMode = VK_SHARING_MODE_CONCURRENT,
        // .queueFamilyIndexCount = 1,
        // .pQueueFamilyIndices = &vk->queue_family,
    }), nullptr, &buffer.buffer));

    VkMemoryRequirements mem_reqs;
    vk->GetBufferMemoryRequirements(vk->device, buffer.buffer, &mem_reqs);

    vk_check(vk->AllocateMemory(vk->device, ptr_to(VkMemoryAllocateInfo {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = ptr_to(VkMemoryAllocateFlagsInfo {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
            .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
        }),
        .allocationSize = mem_reqs.size,
        .memoryTypeIndex = vk_find_memory_type(vk, mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    }), nullptr, &buffer.memory));

    vk_check(vk->BindBufferMemory(vk->device, buffer.buffer, buffer.memory, 0));

    vk_check(vk->MapMemory(vk->device, buffer.memory, 0, size, 0, &buffer.host_address));

    buffer.device_address = vk->GetBufferDeviceAddress(vk->device, ptr_to(VkBufferDeviceAddressInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = buffer.buffer,
    }));

    return buffer;
}

void vk_buffer_destroy(VulkanContext* vk, const VulkanBuffer& buffer)
{
    vk->DestroyBuffer(vk->device, buffer.buffer, nullptr);
    vk->FreeMemory(vk->device, buffer.memory, nullptr);
}

// -----------------------------------------------------------------------------

VulkanImage vk_image_create(VulkanContext* vk, VkExtent2D extent, const void* data)
{
    VulkanImage image = {};

    image.extent = { extent.width, extent.height, 1 };

    auto format = VK_FORMAT_R8G8B8A8_UNORM;
    // auto format = VK_FORMAT_R8G8B8A8_SRGB;

    vk_check(vk->CreateImage(vk->device, ptr_to(VkImageCreateInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = image.extent,
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    }), nullptr, &image.image));

    VkMemoryRequirements mem_reqs;
    vk->GetImageMemoryRequirements(vk->device, image.image, &mem_reqs);

    vk_check(vk->AllocateMemory(vk->device, ptr_to(VkMemoryAllocateInfo {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mem_reqs.size,
        .memoryTypeIndex = vk_find_memory_type(vk, mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    }), nullptr, &image.memory));

    vk_check(vk->BindImageMemory(vk->device, image.image, image.memory, 0));

    {
        auto cmd = vulkan_context_begin_commands(vk);

        constexpr auto pixel_size = 4;
        auto row_length = extent.width;
        auto image_height = row_length * extent.height;
        auto image_size = image_height * pixel_size;

        auto buffer = vk_buffer_create(vk, image_size);
        defer { vk_buffer_destroy(vk, buffer); };

        std::memcpy(buffer.host_address, data, image_size);

        vk_transition(vk, cmd, image.image,
            0, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            0, VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        vk->CmdCopyBufferToImage(cmd, buffer.buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, ptr_to(VkBufferImageCopy {
            .bufferOffset = 0,
            .bufferRowLength = row_length,
            .bufferImageHeight = image_size,
            .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .imageOffset = {},
            .imageExtent = { extent.width, extent.height, 1 },
        }));

        vk_transition(vk, cmd, image.image,
            0, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            0, VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);

        vulkan_context_submit_commands(vk, cmd);
    }

    vk_check(vk->CreateImageView(vk->device, ptr_to(VkImageViewCreateInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    }), nullptr, &image.view));

    return image;
}

void vk_image_destroy(VulkanContext* vk, const VulkanImage& image)
{
    vk->DestroyImageView(vk->device, image.view, nullptr);
    vk->DestroyImage(vk->device, image.image, nullptr);
    vk->FreeMemory(vk->device, image.memory, nullptr);
}

// -----------------------------------------------------------------------------

VkSampler vk_sampler_create(VulkanContext* vk)
{
    VkSampler sampler;
    vk_check(vk->CreateSampler(vk->device, ptr_to(VkSamplerCreateInfo {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        // .magFilter = VK_FILTER_LINEAR,
        // .minFilter = VK_FILTER_LINEAR,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .anisotropyEnable = false,
        .maxLod = VK_LOD_CLAMP_NONE,
        .borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
    }), nullptr, &sampler));

    return sampler;
}

void vk_sampler_destroy(VulkanContext* vk, VkSampler sampler)
{
    vk->DestroySampler(vk->device, sampler, nullptr);
}
