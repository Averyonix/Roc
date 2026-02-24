#pragma once

#include "core/pch.hpp"

#define GPU_INSTANCE_FUNCTIONS(DO) \
    DO(EnumeratePhysicalDevices) \
    DO(GetPhysicalDeviceProperties2) \
    DO(GetPhysicalDeviceQueueFamilyProperties) \
    DO(EnumerateDeviceExtensionProperties) \
    DO(CreateDevice) \
    DO(GetDeviceProcAddr) \
    DO(DestroyDevice) \
    DO(DestroyInstance) \
    DO(GetPhysicalDeviceMemoryProperties) \
    DO(GetPhysicalDeviceFormatProperties2) \
    DO(GetPhysicalDeviceImageFormatProperties2) \
    DO(GetPhysicalDeviceToolProperties) \
    DO(CreateDebugUtilsMessengerEXT) \
    DO(DestroyDebugUtilsMessengerEXT) \

#define GPU_DEVICE_FUNCTIONS(DO) \
    DO(GetDeviceQueue) \
    DO(CreateCommandPool) \
    DO(AllocateCommandBuffers) \
    DO(FreeCommandBuffers) \
    DO(CreateSemaphore) \
    DO(CreatePipelineLayout) \
    DO(CreateDescriptorPool) \
    DO(CreateGraphicsPipelines) \
    DO(CreateComputePipelines) \
    DO(WaitForFences) \
    DO(ResetFences) \
    DO(QueueWaitIdle) \
    DO(DestroyImageView) \
    DO(CreateImageView) \
    DO(CmdPipelineBarrier2) \
    DO(BeginCommandBuffer) \
    DO(CmdBeginRendering) \
    DO(CmdSetViewport) \
    DO(CmdSetScissor) \
    DO(CmdBindPipeline) \
    DO(CmdBindIndexBuffer) \
    DO(CmdDraw) \
    DO(CmdDrawIndexed) \
    DO(CmdEndRendering) \
    DO(EndCommandBuffer) \
    DO(QueueSubmit2) \
    DO(GetSemaphoreCounterValue) \
    DO(WaitSemaphores) \
    DO(DestroyCommandPool) \
    DO(DestroySemaphore) \
    DO(DestroyPipelineLayout) \
    DO(DestroyPipeline) \
    DO(CreateFence) \
    DO(DestroyFence) \
    DO(DestroyDescriptorPool) \
    DO(CmdClearColorImage) \
    DO(ResetCommandPool) \
    DO(CreateBuffer) \
    DO(GetBufferMemoryRequirements) \
    DO(AllocateMemory) \
    DO(BindBufferMemory) \
    DO(DestroyBuffer) \
    DO(FreeMemory) \
    DO(MapMemory) \
    DO(GetBufferDeviceAddress) \
    DO(CmdPushConstants) \
    DO(DestroyImage) \
    DO(CreateImage) \
    DO(GetImageMemoryRequirements) \
    DO(BindImageMemory) \
    DO(CmdCopyImage2) \
    DO(CmdCopyBufferToImage) \
    DO(CmdCopyImageToBuffer) \
    DO(CreateSampler) \
    DO(DestroySampler) \
    DO(CreateDescriptorSetLayout) \
    DO(AllocateDescriptorSets) \
    DO(DestroyDescriptorSetLayout) \
    DO(UpdateDescriptorSets) \
    DO(CmdBindDescriptorSets) \
    DO(SetDebugUtilsObjectNameEXT) \
    DO(CmdBlitImage2) \
    DO(GetMemoryFdPropertiesKHR) \
    DO(GetImageMemoryRequirements2) \
    DO(BindImageMemory2) \
    DO(ImportSemaphoreFdKHR) \
    DO(GetSemaphoreFdKHR) \
    DO(SignalSemaphore) \
    DO(GetImageDrmFormatModifierPropertiesEXT) \
    DO(GetImageSubresourceLayout) \
    DO(GetMemoryFdKHR) \
    DO(CmdDispatch) \

#define GPU_DECLARE_FUNCTION(funcName, ...) PFN_vk##funcName funcName;

struct gpu_context;

void gpu_init_functions(gpu_context*, PFN_vkGetInstanceProcAddr);
void gpu_load_instance_functions(gpu_context*);
void gpu_load_device_functions(gpu_context*);
