#pragma once

#include "compositor/server.hpp"

#include "vulkan_context.hpp"
#include "vulkan_helpers.hpp"

struct Renderer
{
    Server* server;

    VulkanContext* vk;

    VulkanImage image;
};

void renderer_init(Server*);
void renderer_destroy(Renderer*);