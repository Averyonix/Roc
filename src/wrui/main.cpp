#include "wrui.hpp"

#include "wrio/wrio.hpp"

int main()
{
    auto event_loop = wrei_event_loop_create();
    auto wren = wren_create({}, event_loop.get());
    auto wrio = wrio_create(event_loop.get(), wren.get());
    auto wrui = wrui_create(wren.get(), wrio.get());

    auto texture = wrui_texture_create(wrui.get());
    wrui_node_set_transform(texture.get(), wrui_get_scene(wrui.get()).transform);
    wrui_tree_place_below(wrui_get_scene(wrui.get()).tree, nullptr, texture.get());
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

    auto window = wrui_window_create(wrui.get());
    auto initial_size = vec2f32{256, 256};
    wrui_window_set_size(window.get(), initial_size);

    auto canvas = wrui_texture_create(wrui.get());
    wrui_texture_set_tint(canvas.get(), {255, 0, 255, 255});
    wrui_texture_set_dst(canvas.get(), {{}, initial_size, wrei_xywh});
    wrui_node_set_transform(canvas.get(), wrui_window_get_transform(window.get()));
    wrui_tree_place_below(wrui_window_get_tree(window.get()), nullptr, canvas.get());

    auto transform = wrui_transform_create(wrui.get());
    wrui_node_set_transform(transform.get(), wrui_window_get_transform(window.get()));
    wrui_transform_update(transform.get(), {64, 64}, 1);

    auto square = wrui_texture_create(wrui.get());
    wrui_texture_set_tint(square.get(), {0, 255, 255, 255});
    wrui_texture_set_dst(square.get(), {{}, {128, 128}, wrei_xywh});
    wrui_node_set_transform(square.get(), transform.get());
    wrui_tree_place_above(wrui_window_get_tree(window.get()), nullptr, square.get());

    wrui_transform_update(wrui_window_get_transform(window.get()), {64, 64}, 2);

    wrui_window_set_event_handler(window.get(), [canvas = canvas.get()](wrui_event* event) {
        switch (event->type) {
            break;case wrui_event_type::resize:
                wrui_texture_set_dst(canvas, {{}, event->resize, wrei_xywh});
                wrui_window_set_size(event->window, event->resize);
        }
    });
    wrui_window_map(window.get());

    wrio_run(wrio.get());
}
