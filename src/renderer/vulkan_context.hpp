#pragma once

#include "common/types.hpp"
#include "vulkan_functions.hpp"

struct VulkanContext
{
    VULKAN_DECLARE_FUNCTION(GetInstanceProcAddr)
    VULKAN_DECLARE_FUNCTION(CreateInstance)
    VULKAN_INSTANCE_FUNCTIONS(VULKAN_DECLARE_FUNCTION)
    VULKAN_DEVICE_FUNCTIONS(  VULKAN_DECLARE_FUNCTION)

    void* vulkan1;

    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;

    vkwsi_context* vkwsi;

    u32 queue_family;
    VkQueue queue;

    VkCommandPool cmd_pool;
    VkCommandBuffer cmd;
};

VulkanContext* vulkan_context_create(struct Backend*);
void           vulkan_context_destroy(VulkanContext* vk);

VkCommandBuffer vulkan_context_begin_commands( VulkanContext*);
void            vulkan_context_submit_commands(VulkanContext*, VkCommandBuffer);
