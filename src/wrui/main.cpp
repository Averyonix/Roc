#include "wrui.hpp"

#include "wrio/wrio.hpp"

int main()
{
    auto event_loop = wrei_event_loop_create();
    auto wren = wren_create({}, event_loop.get());
    auto wrio = wrio_create(event_loop.get(), wren.get());
    auto wrui = wrui_create(wren.get(), wrio.get());

    auto scene = wrui_get_scene(wrui.get());

    auto texture = wrui_texture_create(wrui.get());
    wrui_node_set_transform(texture.get(), scene.transform);
    wrui_tree_place_above(scene.tree, nullptr, texture.get());

    {
        std::filesystem::path path = getenv("WALLPAPER");

        int w, h;
        int num_channels;
        stbi_uc* data = stbi_load(path.c_str(), &w, &h, &num_channels, STBI_rgb_alpha);
        defer { stbi_image_free(data); };

        log_info("Loaded background ({}, width = {}, height = {})", path.c_str(), w, h);

        auto image = wren_image_create(wren.get(), {w, h}, wren_format_from_drm(DRM_FORMAT_ABGR8888),
            wren_image_usage::texture | wren_image_usage::transfer);
        wren_image_update_immed(image.get(), data);

        wrui_texture_set_image(texture.get(), image.get());
        wrui_texture_set_dst(texture.get(), {{}, {w, h}, wrei_xywh});
    }

    wrio_run(wrio.get());
}
