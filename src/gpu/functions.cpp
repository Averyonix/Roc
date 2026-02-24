#include "functions.hpp"
#include "gpu.hpp"

#include "core/log.hpp"

#define VULKAN_LOAD_INSTANCE_FUNCTION(FuncName, ...) \
    ctx->vk.FuncName = (PFN_vk##FuncName)ctx->vk.GetInstanceProcAddr(ctx->instance, "vk"#FuncName); \
    if (!ctx->vk.FuncName) log_error("Failed to load vk" #FuncName);
#define VULKAN_LOAD_DEVICE_FUNCTION(  FuncName, ...) \
    ctx->vk.FuncName = (PFN_vk##FuncName)ctx->vk.GetDeviceProcAddr(  ctx->device,   "vk"#FuncName); \
    if (!ctx->vk.FuncName) log_error("Failed to load vk" #FuncName);

void gpu_init_functions(gpu_context* ctx, PFN_vkGetInstanceProcAddr loadFn)
{
    ctx->vk.GetInstanceProcAddr = loadFn;

    VULKAN_LOAD_INSTANCE_FUNCTION(CreateInstance)
}

void gpu_load_instance_functions(gpu_context* ctx)
{
    GPU_INSTANCE_FUNCTIONS(VULKAN_LOAD_INSTANCE_FUNCTION)
}

void gpu_load_device_functions(gpu_context* ctx)
{
    GPU_DEVICE_FUNCTIONS(VULKAN_LOAD_DEVICE_FUNCTION)
}
