#include "internal.hpp"

ref<wren_buffer> wren_buffer_create(wren_context* ctx, usz size)
{
    auto buffer = wrei_create<wren_buffer>();
    buffer->ctx = ctx;

    buffer->size = size;

    ctx->stats.active_buffers++;

    VmaAllocationInfo alloc_info;
    wren_check(vmaCreateBuffer(ctx->vma, wrei_ptr_to(VkBufferCreateInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
               | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
               | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
               | VK_BUFFER_USAGE_TRANSFER_DST_BIT
               | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    }), wrei_ptr_to(VmaAllocationCreateInfo {
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    }), &buffer->buffer, &buffer->vma_allocation, &alloc_info));

    ctx->stats.active_buffer_memory += alloc_info.size;

    buffer->host_address = alloc_info.pMappedData;

    buffer->device_address = ctx->vk.GetBufferDeviceAddress(ctx->device, wrei_ptr_to(VkBufferDeviceAddressInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = buffer->buffer,
    }));

    return buffer;
}

wren_buffer::~wren_buffer()
{
    ctx->stats.active_buffers--;

    VmaAllocationInfo alloc_info;
    vmaGetAllocationInfo(ctx->vma, vma_allocation, &alloc_info);
    ctx->stats.active_buffer_memory -= alloc_info.size;

    vmaDestroyBuffer(ctx->vma, buffer, vma_allocation);
}
