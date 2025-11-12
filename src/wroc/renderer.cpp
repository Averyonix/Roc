#include "server.hpp"

#include "wren/wren.hpp"

void wroc_renderer_create(wroc_server* server)
{
    auto* renderer = server->renderer = new wroc_renderer {};
    renderer->server = server;

    renderer->wren = wren_create();

    std::filesystem::path path = getenv("WALLPAPER");

    int w, h;
    int num_channels;
    stbi_uc* data = stbi_load(path.c_str(), &w, &h, &num_channels, STBI_rgb_alpha);

    log_info("Loaded image ({}, width = {}, height = {})", path.c_str(), w, h);

    renderer->image = wren_image_create(renderer->wren, { u32(w), u32(h) }, data);
}

void wroc_renderer_destroy(wroc_server* server)
{
    auto* renderer = server->renderer;
    wren_image_destroy(renderer->wren, renderer->image);
    vkwsi_context_destroy(renderer->wren->vkwsi);
    wren_destroy(renderer->wren);
}
