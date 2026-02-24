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

    // Pointer driver

    scene_pointer_set_driver(scene.get(), [scene = scene.get()](scene_pointer_driver_in in) -> scene_pointer_driver_out {

        // Apply a linear mouse acceleration curve
        //
        // Offset     - speed before acceleration is applied.
        // Accel      - rate that sensitivity increases with motion.
        // Multiplier - total multplier for sensitivity.
        //
        //      /
        //     / <- Accel
        // ___/
        //  ^-- Offset

        static constexpr f32 offset     = 2.f;
        static constexpr f32 rate       = 0.05f;
        static constexpr f32 multiplier = 0.3f;

        f32 speed = glm::length(in.delta);
        vec2f32 sens = vec2f32(multiplier * (1 + (std::max(speed, offset) - offset) * rate));
        vec2f32 delta = in.delta * sens;

        vec2f32 new_pos = in.position + delta;

        // Clamp new position to output layout

        vec2f32 best_position = new_pos;
        f32 best_distance = INFINITY;
        for (auto* output : scene_list_outputs(scene)) {
            auto clamped = core_rect_clamp_point(scene_output_get_viewport(output), new_pos);
            if (new_pos == clamped) {
                best_position = new_pos;
                break;
            } else if (f32 dist = glm::distance(clamped, new_pos); dist < best_distance) {
                best_position = clamped;
                best_distance = dist;
            }
        }

        return {
            .position = best_position,
            .accel    = delta,
            .unaccel  = in.delta
        };
    });

    // Background

    auto sampler = gpu_sampler_create(gpu.get(), VK_FILTER_NEAREST, VK_FILTER_LINEAR);

    auto background_image = [&] {
        std::filesystem::path path = getenv("WALLPAPER");
        int w, h;
        int num_channels;
        stbi_uc* data = stbi_load(path.c_str(), &w, &h, &num_channels, STBI_rgb_alpha);
        defer { stbi_image_free(data); };
        log_info("Loaded background ({}, width = {}, height = {})", path.c_str(), w, h);

        // Create background texture node
        auto image = gpu_image_create(gpu.get(), {w, h}, gpu_format_from_drm(DRM_FORMAT_XBGR8888),
            gpu_image_usage::texture | gpu_image_usage::transfer);
        gpu_image_update_immed(image.get(), data);
        return image;
    }();

    auto background_client = scene_client_create(scene.get());

    ref<scene_tree> background_layer;
    auto update_backgrounds = [&] {
        auto root = scene_get_root_transform(scene.get());

        if (background_layer) scene_node_unparent(background_layer.get());
        background_layer = scene_tree_create(scene.get());
        scene_tree_place_above(scene_get_layer(scene.get(), scene_layer::background), nullptr, background_layer.get());

        for (auto* output : scene_list_outputs(scene.get())) {
            vec2f32 image_size = background_image->extent;
            auto viewport = scene_output_get_viewport(output);

            // Create input sink
            auto input = scene_input_plane_create(background_client.get());
            scene_input_plane_set_rect(input.get(), viewport);
            scene_node_set_transform(input.get(), scene_get_root_transform(scene.get()));
            scene_tree_place_above(background_layer.get(), nullptr, input.get());

            // Create texture node
            auto texture = scene_texture_create(scene.get());
            scene_texture_set_image(texture.get(), background_image.get(), sampler.get(), gpu_blend_mode::premultiplied);
            auto src = core_rect_fit<f32>(image_size, viewport.extent);
            scene_texture_set_src(texture.get(), {src.origin / image_size, src.extent / image_size, core_xywh});
            scene_texture_set_dst(texture.get(), viewport);
            scene_node_set_transform(texture.get(), root);
            scene_tree_place_above(background_layer.get(), nullptr, texture.get());
        }
    };

    scene_client_set_event_handler(background_client.get(), [scene = scene.get(), &update_backgrounds](scene_event* event) {
        switch (event->type) {
            break;case scene_event_type::pointer_button:
                if (event->pointer.button.pressed) {
                    log_warn("Background clicked, dropping keyboard grabs");
                    scene_keyboard_clear_focus(scene);
                }
            break;case scene_event_type::output_layout:
                update_backgrounds();
            break;case scene_event_type::keyboard_key:
                  case scene_event_type::keyboard_modifier:
                  case scene_event_type::pointer_motion:
                  case scene_event_type::pointer_scroll:
                  case scene_event_type::focus_keyboard:
                  case scene_event_type::focus_pointer:
                  case scene_event_type::window_resize:
                  case scene_event_type::redraw:
                ;
        }
    });

    // Test client

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

    scene_client_set_event_handler(client.get(), [&](scene_event* event) {
        switch (event->type) {
            break;case scene_event_type::keyboard_key:
                log_trace("keyboard_key({}, {})",
                    libevdev_event_code_get_name(EV_KEY, event->key.code),
                    event->key.pressed ? "pressed" : "released");
            break;case scene_event_type::keyboard_modifier:
                log_trace("keyboard_modifier({})", core_to_string(scene_keyboard_get_modifiers(scene.get())));
            break;case scene_event_type::pointer_motion:
                log_trace("pointer_motion(accel: {}, unaccel: {}, pos: {})",
                    core_to_string(event->pointer.motion.rel_accel),
                    core_to_string(event->pointer.motion.rel_unaccel),
                    core_to_string(scene_pointer_get_position(scene.get())));
            break;case scene_event_type::pointer_button:
                log_trace("pointer_button({}, {})",
                    libevdev_event_code_get_name(EV_KEY, event->pointer.button.code),
                    event->pointer.button.pressed ? "pressed" : "released");
                scene_keyboard_grab(client.get());
                scene_window_raise(window.get());
            break;case scene_event_type::pointer_scroll:
                log_trace("pointer_scroll(delta: {})", core_to_string(event->pointer.scroll.delta));
            break;case scene_event_type::focus_pointer:
                log_trace("focus_pointer({} -> {})", (void*)event->focus.lost.client, (void*)event->focus.gained.client);
            break;case scene_event_type::focus_keyboard:
                log_trace("focus_keyboard({} -> {})", (void*)event->focus.lost.client, (void*)event->focus.gained.client);
            break;case scene_event_type::window_resize:
                scene_texture_set_dst(canvas.get(), {{}, event->window.resize, core_xywh});
                scene_window_set_size(event->window.window, event->window.resize);
            break;case scene_event_type::redraw:
                  case scene_event_type::output_layout:
                ;
        }
    });

    scene_window_map(window.get());

    // Wayland

    auto way = way_create(event_loop.get(), gpu.get(), scene.get());

    // ImGui

    auto imui = imui_create(gpu.get(), scene.get());
    imui_add_frame_handler(imui.get(), [&] {
        ImGui::ShowDemoWindow();

        defer { ImGui::End(); };
        if (ImGui::Begin("Roc")) {
            if (ImGui::Button("New Output")) {
                io_add_output(io.get());
            }
        }
    });
    imui_request_frame(imui.get());

    // Run

    io_run(io.get());
}
