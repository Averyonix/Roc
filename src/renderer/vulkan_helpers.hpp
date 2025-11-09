#pragma once

#include "common/types.hpp"
#include "common/log.hpp"

#include "vulkan_include.hpp"

struct VulkanContext;

const char* vk_result_to_string(VkResult res);

VkResult vk_check(VkResult res, auto... allowed)
{
    if (res == VK_SUCCESS || (... || (res == allowed))) return res;

    log_error("VULKAN ERROR: {}, ({})", vk_result_to_string(res), int(res));

    return res;
}

template<typename Container, typename Fn, typename... Args>
void vk_enumerate(Container& container, Fn&& fn, Args&&... args)
{
    u32 count = static_cast<u32>(container.size());
    for (;;) {
        u32 old_count = count;
        if constexpr (std::same_as<VkResult, decltype(fn(args..., &count, nullptr))>) {
            vk_check(fn(args..., &count, container.data()), VK_INCOMPLETE);
        } else {
            fn(args..., &count, container.data());
        }

        container.resize(count);
        if (count <= old_count) return;
    }
}

inline
auto vk_make_chain_in(std::span<void* const> structures)
{
    VkBaseInStructure* last = nullptr;
    for (auto* s : structures) {
        auto vk_base = static_cast<VkBaseInStructure*>(s);
        vk_base->pNext = last;
        last = vk_base;
    }

    return last;
};

struct VulkanBuffer
{
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDeviceAddress device_address;
    void* host_address;

    template<typename T>
    T* device() const { return reinterpret_cast<T*>(device_address); }

    template<typename T>
    T* host() const { return reinterpret_cast<T*>(host_address); }
};

VulkanBuffer vk_buffer_create(VulkanContext*, usz size);
void vk_buffer_destroy(VulkanContext* vk, const VulkanBuffer&);

u32 vk_find_memory_type(VulkanContext* vk, u32 type_filter, VkMemoryPropertyFlags properties);

struct VulkanImage
{
    VkImage image;
    VkImageView view;
    VkDeviceMemory memory;
    VkExtent3D extent;
};

VulkanImage vk_image_create(VulkanContext*, VkExtent2D extent, const void* data);
void vk_image_destroy(VulkanContext* vk, const VulkanImage&);

VkSampler vk_sampler_create(VulkanContext*);
void vk_sampler_destroy(VulkanContext*, VkSampler);

void vk_transition(VulkanContext* vk, VkCommandBuffer cmd, VkImage image,
        VkPipelineStageFlags2 src, VkPipelineStageFlags2 dst,
        VkAccessFlags2 src_access, VkAccessFlags2 dst_access,
        VkImageLayout old_layout, VkImageLayout new_layout);
