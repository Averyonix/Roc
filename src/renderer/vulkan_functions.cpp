#include "vulkan_functions.hpp"
#include "vulkan_context.hpp"

#include "common/log.hpp"

#define VULKAN_LOAD_INSTANCE_FUNCTION(funcName, ...) \
    vk->funcName = (PFN_vk##funcName)vk->GetInstanceProcAddr(vk->instance, "vk"#funcName); \
    if (!vk->funcName) log_error("failed to load vk" #funcName);
#define VULKAN_LOAD_DEVICE_FUNCTION(  funcName, ...) \
    vk->funcName = (PFN_vk##funcName)vk->GetDeviceProcAddr(  vk->device,   "vk"#funcName); \
    if (!vk->funcName) log_error("failed to load vk" #funcName);

void vulkan_init_functions(VulkanContext* vk, PFN_vkGetInstanceProcAddr loadFn)
{
    vk->GetInstanceProcAddr = loadFn;

    VULKAN_LOAD_INSTANCE_FUNCTION(CreateInstance)
}

void vulkan_load_instance_functions(VulkanContext* vk)
{
    VULKAN_INSTANCE_FUNCTIONS(VULKAN_LOAD_INSTANCE_FUNCTION)
}

void vulkan_load_device_functions(VulkanContext* vk)
{
    VULKAN_DEVICE_FUNCTIONS(VULKAN_LOAD_DEVICE_FUNCTION)
}
