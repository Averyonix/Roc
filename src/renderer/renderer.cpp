#include "renderer.hpp"

#include <stb_image.h>

void renderer_init(Display* display)
{
    auto* renderer = display->renderer = new Renderer {};
    renderer->display = display;

    renderer->vk = vulkan_context_create(display->backend);

    std::filesystem::path path = getenv("WALLPAPER");

    int w, h;
    int num_channels;
    stbi_uc* data = stbi_load(path.c_str(), &w, &h, &num_channels, STBI_rgb_alpha);

    log_info("Loaded image ({}, width = {}, height = {})", path.c_str(), w, h);

    renderer->image = vk_image_create(renderer->vk, { u32(w), u32(h) }, data);
}
