#pragma once

#include "compositor/display.hpp"

#include "vulkan_context.hpp"
#include "vulkan_helpers.hpp"

struct Renderer
{
    Display* display;

    VulkanContext* vk;

    VulkanImage image;
};

void renderer_init(Display*);
