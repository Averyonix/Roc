#pragma once

#include "wrei/types.hpp"
#include "wren_functions.hpp"

struct wren_context
{
    struct {
        WREN_DECLARE_FUNCTION(GetInstanceProcAddr)
        WREN_DECLARE_FUNCTION(CreateInstance)
        WREN_INSTANCE_FUNCTIONS(WREN_DECLARE_FUNCTION)
        WREN_DEVICE_FUNCTIONS(  WREN_DECLARE_FUNCTION)
    } vk;

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

wren_context* wren_create();
void          wren_destroy(wren_context*);

VkCommandBuffer wren_begin_commands( wren_context*);
void            wren_submit_commands(wren_context*, VkCommandBuffer);
