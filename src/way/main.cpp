#include "io/io.hpp"

#include "way.hpp"

WROC_NAMESPACE_USE

int main()
{
    auto event_loop = wrei_event_loop_create();
    auto wren = wren_create({}, event_loop.get());
    auto wrio = wrio_create(event_loop.get(), wren.get());
    auto wrui = wrui_create(wren.get(), wrio.get());

    auto sampler = wren_sampler_create(wren.get(), VK_FILTER_NEAREST, VK_FILTER_LINEAR);

    auto texture = wrui_texture_create(wrui.get());
    wrui_node_set_transform(texture.get(), wrui_get_root_transform(wrui.get()));
    wrui_tree_place_below(wrui_get_layer(wrui.get(), wrui_layer::background), nullptr, texture.get());
    {
        std::filesystem::path path = getenv("WALLPAPER");

        int w, h;
        int num_channels;
        stbi_uc* data = stbi_load(path.c_str(), &w, &h, &num_channels, STBI_rgb_alpha);
        defer { stbi_image_free(data); };

        log_info("Loaded background ({}, width = {}, height = {})", path.c_str(), w, h);

        auto image = wren_image_create(wren.get(), {w, h}, wren_format_from_drm(DRM_FORMAT_XBGR8888),
            wren_image_usage::texture | wren_image_usage::transfer);
        wren_image_update_immed(image.get(), data);

        wrui_texture_set_image(texture.get(), image.get(), sampler.get(), wren_blend_mode::premultiplied);
        wrui_texture_set_dst(texture.get(), {{}, {w, h}, wrei_xywh});
    }

    auto wroc = wroc_create(event_loop.get(), wren.get(), wrui.get());

    wrio_run(wrio.get());
}
