#pragma once

#include "compositor/display.hpp"

#include "vulkan_context.hpp"

struct Renderer
{
    Display* display;

    VulkanContext* vk;
};

void renderer_init(Display*);
