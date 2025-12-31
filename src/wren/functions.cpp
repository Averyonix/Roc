#include "functions.hpp"
#include "wren.hpp"

#include "wrei/log.hpp"

#define VULKAN_LOAD_INSTANCE_FUNCTION(FuncName, ...) \
    ctx->vk.FuncName = (PFN_vk##FuncName)ctx->vk.GetInstanceProcAddr(ctx->instance, "vk"#FuncName); \
    if (!ctx->vk.FuncName) log_error("failed to load vk" #FuncName);
#define VULKAN_LOAD_DEVICE_FUNCTION(  FuncName, ...) \
    ctx->vk.FuncName = (PFN_vk##FuncName)ctx->vk.GetDeviceProcAddr(  ctx->device,   "vk"#FuncName); \
    if (!ctx->vk.FuncName) log_error("failed to load vk" #FuncName);

void wren_init_functions(wren_context* ctx, PFN_vkGetInstanceProcAddr loadFn)
{
    ctx->vk.GetInstanceProcAddr = loadFn;

    VULKAN_LOAD_INSTANCE_FUNCTION(CreateInstance)
}

void wren_load_instance_functions(wren_context* ctx)
{
    WREN_INSTANCE_FUNCTIONS(VULKAN_LOAD_INSTANCE_FUNCTION)
}

void wren_load_device_functions(wren_context* ctx)
{
    WREN_DEVICE_FUNCTIONS(VULKAN_LOAD_DEVICE_FUNCTION)
}
