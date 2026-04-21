#include "functions.hpp"

#include <core/log.hpp>

#define VULKAN_LOAD_INSTANCE_FUNCTION(FuncName, ...) \
    vk.FuncName = (PFN_vk##FuncName)vk.GetInstanceProcAddr(instance, "vk"#FuncName); \
    if (!vk.FuncName) log_error("Failed to load vk" #FuncName);

#define VULKAN_LOAD_DEVICE_FUNCTION(FuncName, ...) \
    vk.FuncName = (PFN_vk##FuncName)vk.GetDeviceProcAddr(device, "vk"#FuncName); \
    if (!vk.FuncName) log_error("Failed to load vk" #FuncName);

void gpu_init_functions(GpuVulkanFunctions& vk, PFN_vkGetInstanceProcAddr loadFn)
{
    vk.GetInstanceProcAddr = loadFn;

    VkInstance instance = nullptr;
    VULKAN_LOAD_INSTANCE_FUNCTION(CreateInstance)
}

void gpu_load_instance_functions(GpuVulkanFunctions& vk, VkInstance instance)
{
    GPU_INSTANCE_FUNCTIONS(VULKAN_LOAD_INSTANCE_FUNCTION)
}

void gpu_load_device_functions(GpuVulkanFunctions& vk, VkDevice device)
{
    GPU_DEVICE_FUNCTIONS(VULKAN_LOAD_DEVICE_FUNCTION)
}
