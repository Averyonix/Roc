#pragma once

#include "vulkan_include.hpp"

#define VULKAN_INSTANCE_FUNCTIONS(DO) \
    DO(EnumeratePhysicalDevices) \
    DO(GetPhysicalDeviceProperties2) \
    DO(GetPhysicalDeviceQueueFamilyProperties) \
    DO(CreateDevice) \
    DO(GetDeviceProcAddr) \
    DO(GetPhysicalDeviceSurfaceFormatsKHR) \
    DO(GetPhysicalDeviceSurfaceCapabilities2KHR) \
    DO(DestroySurfaceKHR) \
    DO(DestroyDevice) \
    DO(DestroyInstance) \
    DO(GetPhysicalDeviceMemoryProperties) \
    DO(CreateWaylandSurfaceKHR)

#define VULKAN_DEVICE_FUNCTIONS(DO) \
    DO(GetDeviceQueue) \
    DO(CreateCommandPool) \
    DO(AllocateCommandBuffers) \
    DO(FreeCommandBuffers) \
    DO(CreateSemaphore) \
    DO(CreatePipelineLayout) \
    DO(CreateDescriptorPool) \
    DO(CreateGraphicsPipelines) \
    DO(WaitForFences) \
    DO(ResetFences) \
    DO(QueueWaitIdle) \
    DO(DestroyImageView) \
    DO(CreateSwapchainKHR) \
    DO(DestroySwapchainKHR) \
    DO(GetSwapchainImagesKHR) \
    DO(CreateImageView) \
    DO(AcquireNextImageKHR) \
    DO(CmdPipelineBarrier2) \
    DO(BeginCommandBuffer) \
    DO(CmdBeginRendering) \
    DO(CmdSetViewport) \
    DO(CmdSetScissor) \
    DO(CmdBindPipeline) \
    DO(CmdDraw) \
    DO(CmdEndRendering) \
    DO(EndCommandBuffer) \
    DO(QueueSubmit2) \
    DO(QueuePresentKHR) \
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
    DO(CmdCopyBufferToImage) \
    DO(CreateSampler) \
    DO(DestroySampler) \
    DO(CreateDescriptorSetLayout) \
    DO(AllocateDescriptorSets) \
    DO(DestroyDescriptorSetLayout) \
    DO(UpdateDescriptorSets) \
    DO(CmdBindDescriptorSets) \
    DO(SetDebugUtilsObjectNameEXT) \
    DO(CmdBlitImage2)

#define VULKAN_DECLARE_FUNCTION(funcName, ...) PFN_vk##funcName funcName;

struct VulkanContext;

void vulkan_init_functions(VulkanContext*, PFN_vkGetInstanceProcAddr);
void vulkan_load_instance_functions(VulkanContext*);
void vulkan_load_device_functions(VulkanContext*);
