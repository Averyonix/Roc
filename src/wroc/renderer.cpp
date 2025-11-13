#include "server.hpp"

#include "wrei/ref.hpp"

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

    renderer->image = wren_image_create(renderer->wren.get(), { u32(w), u32(h) });
    wren_image_update(renderer->image.get(), data);
}

void wroc_renderer_destroy(wroc_server* server)
{
    auto* renderer = server->renderer;

    renderer->image.reset();
    vkwsi_context_destroy(renderer->wren->vkwsi);
    renderer->wren.reset();

    delete renderer;
}
