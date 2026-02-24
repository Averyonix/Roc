#include "scene/scene.hpp"

#include "io/io.hpp"

#include "imui/imui.hpp"
#include "way/way.hpp"

int main()
{
    auto event_loop = core_event_loop_create();
    auto gpu = gpu_create({}, event_loop.get());
    auto io = io_create(event_loop.get(), gpu.get());
    auto scene = scene_create(gpu.get(), io.get());

    auto sampler = gpu_sampler_create(gpu.get(), VK_FILTER_NEAREST, VK_FILTER_LINEAR);

    auto root = scene_get_root_transform(scene.get());
    auto background_layer = scene_get_layer(scene.get(), scene_layer::background);

    auto texture = scene_texture_create(scene.get());
    scene_node_set_transform(texture.get(), root);
    scene_tree_place_above(background_layer, nullptr, texture.get());
    {
        std::filesystem::path path = getenv("WALLPAPER");

        int w, h;
        int num_channels;
        stbi_uc* data = stbi_load(path.c_str(), &w, &h, &num_channels, STBI_rgb_alpha);
        defer { stbi_image_free(data); };

        log_info("Loaded background ({}, width = {}, height = {})", path.c_str(), w, h);

        auto image = gpu_image_create(gpu.get(), {w, h}, gpu_format_from_drm(DRM_FORMAT_XBGR8888),
            gpu_image_usage::texture | gpu_image_usage::transfer);
        gpu_image_update_immed(image.get(), data);

        scene_texture_set_image(texture.get(), image.get(), sampler.get(), gpu_blend_mode::premultiplied);
        scene_texture_set_dst(texture.get(), {{}, {w, h}, core_xywh});
    }

    auto background_client = scene_client_create(scene.get());
    {
        auto background_input_sink = scene_input_plane_create(background_client.get());
        scene_input_plane_set_rect(background_input_sink.get(), {{}, {1920, 1080}, core_xywh});
        scene_node_set_transform(background_input_sink.get(), scene_get_root_transform(scene.get()));
        scene_tree_place_above(background_layer, nullptr, background_input_sink.get());
    }
    scene_client_set_event_handler(background_client.get(), [scene = scene.get()](scene_event* event) {
        if (event->type == scene_event_type::pointer_button && event->pointer.pressed) {
            log_warn("Background clicked, dropping keyboard grabs");
            scene_keyboard_clear_focus(scene);
        }
    });

    auto client = scene_client_create(scene.get());

    auto window = scene_window_create(client.get());
    auto initial_size = vec2f32{256, 256};
    scene_window_set_size(window.get(), initial_size);

    auto canvas = scene_texture_create(scene.get());
    scene_texture_set_tint(canvas.get(), {255, 0, 255, 255});
    scene_texture_set_dst(canvas.get(), {{}, initial_size, core_xywh});
    scene_node_set_transform(canvas.get(), scene_window_get_transform(window.get()));
    scene_tree_place_below(scene_window_get_tree(window.get()), nullptr, canvas.get());

    auto input = scene_input_plane_create(client.get());
    scene_input_plane_set_rect(input.get(), {{}, initial_size, core_xywh});
    scene_node_set_transform(input.get(), scene_window_get_transform(window.get()));
    scene_tree_place_above(scene_window_get_tree(window.get()), nullptr, input.get());

    auto transform = scene_transform_create(scene.get());
    scene_node_set_transform(transform.get(), scene_window_get_transform(window.get()));
    scene_transform_update(transform.get(), {64, 64}, 1);

    auto square = scene_texture_create(scene.get());
    scene_texture_set_tint(square.get(), {0, 255, 255, 255});
    scene_texture_set_dst(square.get(), {{}, {128, 128}, core_xywh});
    scene_node_set_transform(square.get(), transform.get());
    scene_tree_place_above(scene_window_get_tree(window.get()), nullptr, square.get());

    scene_client_set_event_handler(client.get(), [client = client.get(), canvas = canvas.get(), scene = scene.get()](scene_event* event) {
        switch (event->type) {
            break;case scene_event_type::keyboard_key:
                log_warn("keyboard_key({}, {})", libevdev_event_code_get_name(EV_KEY, event->key.code), event->pointer.pressed ? "pressed" : "released");
            break;case scene_event_type::keyboard_modifier:
                log_warn("keyboard_modifier({})", core_to_string(scene_keyboard_get_modifiers(scene)));
            break;case scene_event_type::pointer_motion:
                log_warn("pointer_motion(delta: {}, pos: {})", core_to_string(event->pointer.delta), core_to_string(scene_pointer_get_position(scene)));
            break;case scene_event_type::pointer_button:
                log_warn("pointer_button({}, {})", libevdev_event_code_get_name(EV_KEY, event->pointer.button), event->pointer.pressed ? "pressed" : "released");
                scene_keyboard_grab(client);
            break;case scene_event_type::pointer_scroll:
                log_warn("pointer_scroll(delta: {})", core_to_string(event->pointer.delta));
            break;case scene_event_type::focus_pointer:
                log_warn("focus_pointer({} -> {})", (void*)event->focus.lost.client, (void*)event->focus.gained.client);
            break;case scene_event_type::focus_keyboard:
                log_warn("focus_keyboard({} -> {})", (void*)event->focus.lost.client, (void*)event->focus.gained.client);
            break;case scene_event_type::window_resize:
                scene_texture_set_dst(canvas, {{}, event->window.resize, core_xywh});
                scene_window_set_size(event->window.window, event->window.resize);
            break;case scene_event_type::redraw:
                ;
        }
    });

    scene_window_map(window.get());

    auto way = way_create(event_loop.get(), gpu.get(), scene.get());

    auto imui = imui_create(gpu.get(), scene.get());
    imui_add_frame_handler(imui.get(), [] { ImGui::ShowDemoWindow(); });
    imui_request_frame(imui.get());

    io_run(io.get());
}
