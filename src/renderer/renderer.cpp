#include "renderer.hpp"

#include <stb_image.h>

void renderer_init(Server* server)
{
    auto* renderer = server->renderer = new Renderer {};
    renderer->server = server;

    renderer->vk = vulkan_context_create(server->backend);

    std::filesystem::path path = getenv("WALLPAPER");

    int w, h;
    int num_channels;
    stbi_uc* data = stbi_load(path.c_str(), &w, &h, &num_channels, STBI_rgb_alpha);

    log_info("Loaded image ({}, width = {}, height = {})", path.c_str(), w, h);

    renderer->image = vk_image_create(renderer->vk, { u32(w), u32(h) }, data);
}

void renderer_destroy(Renderer* renderer)
{
    vk_image_destroy(renderer->vk, renderer->image);
    vkwsi_context_destroy(renderer->vk->vkwsi);
    vulkan_context_destroy(renderer->vk);
}
