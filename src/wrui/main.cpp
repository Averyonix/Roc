#include "wrui.hpp"

#include "wrio/wrio.hpp"

int main()
{
    auto event_loop = wrei_event_loop_create();
    auto wren = wren_create({}, event_loop.get());
    auto wrio = wrio_create(event_loop.get(), wren.get());
    auto wrui = wrui_create(wren.get(), wrio.get());

    auto sampler = wren_sampler_create(wren.get(), VK_FILTER_NEAREST, VK_FILTER_LINEAR);

    auto root = wrui_get_root_transform(wrui.get());
    auto background_layer = wrui_get_layer(wrui.get(), wrui_layer::background);

    auto texture = wrui_texture_create(wrui.get());
    wrui_node_set_transform(texture.get(), root);
    wrui_tree_place_above(background_layer, nullptr, texture.get());
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

    auto background_client = wrui_client_create(wrui.get());
    {
        auto background_input_sink = wrui_input_plane_create(background_client.get());
        wrui_input_plane_set_rect(background_input_sink.get(), {{}, {1920, 1080}, wrei_xywh});
        wrui_node_set_transform(background_input_sink.get(), wrui_get_root_transform(wrui.get()));
        wrui_tree_place_above(background_layer, nullptr, background_input_sink.get());
    }
    wrui_client_set_event_handler(background_client.get(), [wrui = wrui.get()](wrui_event* event) {
        if (event->type == wrui_event_type::pointer_button && event->pointer.pressed) {
            log_warn("Background clicked, dropping keyboard grabs");
            wrui_keyboard_clear_focus(wrui);
        }
    });

    auto client = wrui_client_create(wrui.get());

    auto window = wrui_window_create(client.get());
    auto initial_size = vec2f32{256, 256};
    wrui_window_set_size(window.get(), initial_size);

    auto canvas = wrui_texture_create(wrui.get());
    wrui_texture_set_tint(canvas.get(), {255, 0, 255, 255});
    wrui_texture_set_dst(canvas.get(), {{}, initial_size, wrei_xywh});
    wrui_node_set_transform(canvas.get(), wrui_window_get_transform(window.get()));
    wrui_tree_place_below(wrui_window_get_tree(window.get()), nullptr, canvas.get());

    auto input = wrui_input_plane_create(client.get());
    wrui_input_plane_set_rect(input.get(), {{}, initial_size, wrei_xywh});
    wrui_node_set_transform(input.get(), wrui_window_get_transform(window.get()));
    wrui_tree_place_above(wrui_window_get_tree(window.get()), nullptr, input.get());

    auto transform = wrui_transform_create(wrui.get());
    wrui_node_set_transform(transform.get(), wrui_window_get_transform(window.get()));
    wrui_transform_update(transform.get(), {64, 64}, 1);

    auto square = wrui_texture_create(wrui.get());
    wrui_texture_set_tint(square.get(), {0, 255, 255, 255});
    wrui_texture_set_dst(square.get(), {{}, {128, 128}, wrei_xywh});
    wrui_node_set_transform(square.get(), transform.get());
    wrui_tree_place_above(wrui_window_get_tree(window.get()), nullptr, square.get());

    wrui_client_set_event_handler(client.get(), [client = client.get(), canvas = canvas.get(), wrui = wrui.get()](wrui_event* event) {
        switch (event->type) {
            break;case wrui_event_type::keyboard_key:
                log_warn("keyboard_key({}, {})", libevdev_event_code_get_name(EV_KEY, event->key.code), event->pointer.pressed ? "pressed" : "released");
            break;case wrui_event_type::keyboard_modifier:
                log_warn("keyboard_modifier({})", wrei_to_string(wrui_keyboard_get_modifiers(wrui)));
            break;case wrui_event_type::pointer_motion:
                log_warn("pointer_motion(delta: {}, pos: {})", wrei_to_string(event->pointer.delta), wrei_to_string(wrui_pointer_get_position(wrui)));
            break;case wrui_event_type::pointer_button:
                log_warn("pointer_button({}, {})", libevdev_event_code_get_name(EV_KEY, event->pointer.button), event->pointer.pressed ? "pressed" : "released");
                wrui_keyboard_grab(client);
            break;case wrui_event_type::pointer_scroll:
                log_warn("pointer_scroll(delta: {})", wrei_to_string(event->pointer.delta));
            break;case wrui_event_type::focus_pointer:
                log_warn("focus_pointer({} -> {})", (void*)event->focus.lost.client, (void*)event->focus.gained.client);
            break;case wrui_event_type::focus_keyboard:
                log_warn("focus_keyboard({} -> {})", (void*)event->focus.lost.client, (void*)event->focus.gained.client);
            break;case wrui_event_type::window_resize:
                wrui_texture_set_dst(canvas, {{}, event->window.resize, wrei_xywh});
                wrui_window_set_size(event->window.window, event->window.resize);
        }
    });

    wrui_window_map(window.get());

    wrio_run(wrio.get());
}
