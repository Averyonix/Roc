#include "renderer.hpp"

void renderer_init(Display* display)
{
    auto* renderer = display->renderer = new Renderer {};
    renderer->display = display;

    renderer->vk = vulkan_context_create(display->backend);
}
