#include "wroc.hpp"

#include "wrei/object.hpp"

#include "wren/wren.hpp"
#include "wren/internal.hpp"

#include "wroc_blit_shader.hpp"
#include "shaders/blit.h"

#include "wroc/event.hpp"

wroc_renderer::~wroc_renderer() = default;

void wroc_renderer_create(wroc_render_options render_options)
{
    auto* renderer = (server->renderer = wrei_create<wroc_renderer>()).get();
    renderer->options = render_options;

    wren_features features = {};
    if (!(render_options >= wroc_render_options::no_dmabuf)) {
        features |= wren_features::dmabuf;
    }

    renderer->wren = wren_create(features, server->event_loop.get());
    wroc_renderer_init_buffer_feedback(renderer);

    auto* wren = renderer->wren.get();

    std::filesystem::path path = getenv("WALLPAPER");

    int w, h;
    int num_channels;
    stbi_uc* data = stbi_load(path.c_str(), &w, &h, &num_channels, STBI_rgb_alpha);
    defer { stbi_image_free(data); };

    log_info("Loaded image ({}, width = {}, height = {})", path.c_str(), w, h);

    renderer->background = wren_image_create(wren, {w, h}, wren_format_from_drm(DRM_FORMAT_ABGR8888),
        wren_image_usage::texture | wren_image_usage::transfer);
    wren_image_update_immed(renderer->background.get(), data);

    renderer->sampler = wren_sampler_create(wren, VK_FILTER_NEAREST, VK_FILTER_LINEAR);

    renderer->pipeline = wren_pipeline_create(wren,
        wren_blend_mode::premultiplied, server->renderer->output_format,
        wroc_blit_shader, "vertex", "fragment");
}

void wroc_render_frame(wroc_output* output)
{
    auto* renderer = server->renderer.get();
    auto* wren = renderer->wren.get();

    output->frames_in_flight++;

    auto[current, acquire_sync] = wren_swapchain_acquire_image(output->swapchain.get());
    assert(current);
    VkExtent2D vk_extent = { current->extent.x, current->extent.y };
    vec2f32 current_extent = current->extent;

    auto queue = wren_get_queue(wren, wren_queue_type::graphics);
    auto commands = wren_commands_begin(queue);
    auto cmd = commands->buffer;

    struct frame_guard : wrei_object
    {
        wroc_renderer_frame_data frame_data;
        weak<wroc_output> output;

        ~frame_guard()
        {
            server->renderer->available_frames.emplace_back(std::move(frame_data));

            wrei_event_loop_enqueue(server->event_loop.get(), [output = output] {
                if (output) {
                    output->frames_in_flight--;
                    wroc_output_try_dispatch_frame(output.get());
                }
            });
        }
    };
    auto guard = wrei_create<frame_guard>();
    guard->output = output;
    wren_commands_protect_object(commands.get(), guard.get());

    wroc_renderer_frame_data* frame = &guard->frame_data;
    if (!renderer->available_frames.empty()) {
        *frame = std::move(renderer->available_frames.back());
        renderer->available_frames.pop_back();
    } else {
        log_error("RENDERER NEW FRAME");
    }

    wren_transition(wren, commands.get(), current,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        0, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    wren->vk.CmdBeginRendering(cmd, wrei_ptr_to(VkRenderingInfo {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { {}, vk_extent },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = wrei_ptr_to(VkRenderingAttachmentInfo {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = current->view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.color{.float32{0.f, 0.f, 0.f, 1.f}}},
        }),
    }));

    wren->vk.CmdSetViewport(cmd, 0, 1, wrei_ptr_to(VkViewport {
        0, 0,
        current_extent.x, current_extent.y,
        0, 1,
    }));
    wren->vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, wren->pipeline_layout, 0, 1, &wren->set, 0, nullptr);

    auto start_draws = [&] {
        wren->vk.CmdSetScissor(cmd, 0, 1, wrei_ptr_to(VkRect2D { {}, vk_extent }));
        wren->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->pipeline->pipeline);
    };

    u32 rect_id_start = 0;
    u32 rect_id = 0;

    renderer->rects_cpu.clear();

    auto draw = [&](wren_image* image, rect2f64 dest, rect2f64 source, vec4f32 color = {1, 1, 1, 1}) {

        // log_warn("draw(dest = {}, source = {})", wrei_to_string(dest), wrei_to_string(source));

        wren_commands_protect_object(commands.get(), image);

        auto pixel_dst = wroc_output_get_pixel_rect(output, dest);

        rect_id++;
        renderer->rects_cpu.emplace_back(wroc_shader_rect {
            .image = image ? image4f32{image, renderer->sampler.get()} : image4f32 {},
            .image_rect = source,
            .image_has_alpha = image ? image->format->has_alpha : false,
            .rect = pixel_dst,
            .opacity = 1.f,
            .color = color,
        });
    };

    auto flush_draws = [&] {
        if (rect_id_start == rect_id) return;

        if (frame->rects.count < rect_id) {
            auto new_size = wrei_compute_geometric_growth(frame->rects.count, rect_id);
            log_debug("Renderer - reallocating rect buffer, size: {}", new_size);
            if (frame->rects.buffer && rect_id_start) {
                // Previous draws using this buffer, keep alive until all draws complete
                log_debug("  previous buffer still used in draws ({}), keeping...", rect_id_start);
                wren_commands_protect_object(commands.get(), frame->rects.buffer.get());
            }
            frame->rects = {wren_buffer_create(wren, new_size * sizeof(wroc_shader_rect)), new_size};
        }

        if (!renderer->rects_cpu.empty()) {
            std::memcpy(frame->rects.host() + rect_id_start, renderer->rects_cpu.data() + rect_id_start, (rect_id - rect_id_start) * sizeof(wroc_shader_rect));
        }

        wroc_shader_rect_input si = {};
        si.rects = frame->rects.device();
        si.output_size = current_extent;
        wren->vk.CmdPushConstants(cmd, wren->pipeline_layout, VK_SHADER_STAGE_ALL, 0, sizeof(si), &si);
        if (renderer->options >= wroc_render_options::separate_draws) {
            for (u32 i = rect_id_start; i < rect_id; ++i) {
                wren->vk.CmdDraw(cmd, 6, 1, i * 6, 0);
            }
        } else {
            wren->vk.CmdDraw(cmd, 6 * (rect_id - rect_id_start), 1, rect_id_start * 6, 0);
        }
        rect_id_start = rect_id;
    };

    start_draws();

    // Background

    {
        auto src = wrei_rect_fit(server->renderer->background->extent, output->layout_rect.extent);
        draw(server->renderer->background.get(), output->layout_rect, src);
    }

    // Surface helper

    auto draw_surface = [&](this auto&& draw_surface, wroc_surface* surface, vec2f64 pos, vec2f64 scale, f64 opacity = 1.0) -> void {
        if (!surface) return;

        // log_trace("drawing surface: {} (stack.size = {})", (void*)surface, surface->current.surface_stack.size());

        auto* buffer = surface->buffer ? surface->buffer->buffer.get() : nullptr;
        if (!buffer || !buffer->image) return;

        for (auto& s : surface->current.surface_stack) {
            if (s.get() == surface) {

                // Draw self
                rect2f64 dst = surface->buffer_dst;
                dst.origin *= scale;
                dst.extent *= scale;
                dst.origin += pos;

                draw(buffer->image.get(), dst, surface->buffer_src, vec4f32(opacity, opacity, opacity, opacity));

            } else if (auto* subsurface = wroc_surface_get_addon<wroc_subsurface>(s.get())) {

                // Draw subsurface
                draw_surface(s.get(), pos + vec2f64(subsurface->current.position) * scale, scale, opacity);
            }
        }
    };

    // Draw xdg surfaces

    auto draw_xdg_surfaces = [&](bool show_cycled) {
        for (auto* surface : server->surfaces) {
            // TODO: Generic "mapped" state tracking
            if (!surface->buffer) continue;

            auto* xdg_surface = wroc_surface_get_addon<wroc_xdg_surface>(surface);
            if (!xdg_surface) continue;

            auto[pos, scale] = wroc_surface_get_coord_space(surface);

            f32 opacity = 1.0;
            if (server->interaction_mode == wroc_interaction_mode::focus_cycle) {
                if (surface == server->focus.cycled.get()) {
                    if (!show_cycled) continue;
                } else {
                    if (show_cycled) continue;
                    opacity = 0.2;
                }
            }

            rect2f64 layout_rect;
            auto* toplevel = wroc_surface_get_addon<wroc_toplevel>(surface);
            if (toplevel) {
                layout_rect = wroc_toplevel_get_layout_rect(toplevel);

                if (opacity == 1.0) {
                    // Draw backstop under toplevels
                    draw(nullptr, layout_rect, {}, vec4f32{0, 0, 0, 1});
                }
            }

            draw_surface(surface, pos, scale, opacity);

            if (toplevel && !toplevel->fullscreen.output) {

                // Draw focus border above toplevels
                f64     width = 2.0;
                vec4f32 color = surface == server->seat->keyboard->focused_surface.get()
                    ? vec4f32{0.4, 0.4, 1.0, 1.0}
                    : vec4f32{0.3, 0.3, 0.3, 1.0};
                color *= opacity;

                aabb2f64 r = layout_rect;
                draw(nullptr, /* left   */ { r.min - width,     {r.min.x, r.max.y + width}, wrei_minmax}, {}, color);
                draw(nullptr, /* right  */ {{r.max.x, r.min.y - width},  r.max + width,     wrei_minmax}, {}, color);
                draw(nullptr, /* top    */ {{r.min.x, r.min.y - width}, {r.max.x, r.min.y}, wrei_minmax}, {}, color);
                draw(nullptr, /* bottom */ {{r.min.x, r.max.y}, {r.max.x, r.max.y + width}, wrei_minmax}, {}, color);
            }
        }
    };

    draw_xdg_surfaces(false);
    if (server->interaction_mode == wroc_interaction_mode::focus_cycle) {
        draw_xdg_surfaces(true);
    }

    // Draw ImGui

    if (server->imgui->output == output) {
        flush_draws();
        wroc_imgui_frame(server->imgui.get(), current_extent, commands.get());
        start_draws();
    }

    // Draw zone selection

    if (server->interaction_mode == wroc_interaction_mode::zone) {
        auto& c = server->zone.config;
        vec4f32 color = server->zone.selecting ? c.color_selected : c.color_initial;
        color = {vec3f32(color) * color.a, color.a};
        draw(nullptr, server->zone.final_zone, {}, color);
    }

    // Draw drag icon

    if (auto& icon = server->data_manager.drag.icon) {
        draw_surface(icon->surface.get(), server->seat->pointer->position, vec2f64(1.0));
    }

    // Draw cursor

    if (!server->imgui->wants_mouse) {
        auto* pointer = server->seat->pointer.get();

        // TODO: Move this to cursor.cpp
        if (pointer->focused_surface && pointer->focused_surface->cursor) {
            // If surface is focused and has cursor set, render cursor surface (possibly hidden)
            if (auto* cursor_surface = pointer->focused_surface->cursor->get()) {
                draw_surface(cursor_surface->surface.get(), pointer->position, vec2f64(1.0));
            }
        } else {
            // ... else fall back to default cursor
            auto* cursor = server->cursor.get();
            auto& fallback = cursor->fallback;
            auto pos = pointer->position - vec2f64(fallback.hotspot);
            draw(fallback.image.get(), {pos, fallback.image->extent, wrei_xywh}, {{}, fallback.image->extent, wrei_xywh});
        }
    }

    if (renderer->show_debug_cursor) {

        // Debug cursor crosshair

        auto* pointer = server->seat->pointer.get();
        auto pos = pointer->position;

        vec4f32 color = {0.5, 0, 0, 0.5};
        auto length = 24;
        auto width = 2;

        auto hlength = length / 2;
        auto hwidth = width / 2;

        draw(nullptr, {pos - vec2f64{hwidth, hlength}, vec2f64{width, length}, wrei_xywh}, {}, color);
        draw(nullptr, {pos - vec2f64{hlength, hwidth}, vec2f64{length, width}, wrei_xywh}, {}, color);
    }

    // Finish

    flush_draws();

    wren->vk.CmdEndRendering(cmd);

    // Screenshot

#if 0
    if (renderer->screenshot_queued) {
        renderer->screenshot_queued = false;

        // TODO: Factor this out to function and add options (rect, format, etc..)
        // TODO: Also just render everything twice instead of copying?

        wren_submit_commands(wren, cmd);

        auto row_stride = current.extent.width * 4;
        auto byte_size = row_stride * current.extent.height;

        log_warn("Saving screen {}", current.extent.width, current.extent.height);
        log_warn("  row_stride = {}", row_stride);
        log_warn("  row_stride = {}", byte_size);

        std::vector<char> data;
        data.resize(byte_size);

        // TODO: HACK HACK HACK
        //       We should just expose a wren_image instead of directly using the vkwsi returns
        ref<wren_image> image = wrei_create<wren_image>();
        image->image = current.image;
        image->ctx = wren;
        image->extent = vec2u32(current.extent.width, current.extent.height);

        auto t1 = std::chrono::steady_clock::now();

        log_warn("  performing image readback");
        wren_image_readback(image.get(), data.data());

        auto t2 = std::chrono::steady_clock::now();
        log_warn("  image readback completed in {}, saving...", wrei_duration_to_string(t2 - t1));

        // Byte swap BGRA -> RGBA
        for (u32 i = 0; i < byte_size; i += 4) {
            auto b = data[i + 0];
            auto g = data[i + 1];
            auto r = data[i + 2];

            data[i + 0] = r;
            data[i + 1] = g;
            data[i + 2] = b;
        }

        stbi_write_png("screenshot.png", image->extent.x, image->extent.y, STBI_rgb_alpha, data.data(), image->extent.x * 4);
        auto t3 = std::chrono::steady_clock::now();
        log_warn("  save complete in {}", wrei_duration_to_string(t3 - t2));

        cmd = wren_begin_commands(wren);
    }
#endif

    // Submit

    wren_transition(wren, commands.get(), current,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, 0,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    auto present_sema = wren_semaphore_create(wren, VK_SEMAPHORE_TYPE_BINARY);

    wren_commands_submit(commands.get(), {acquire_sync}, {{present_sema.get()}});

    // Present

    wren_swapchain_present(output->swapchain.get(), {{present_sema.get()}});

    if (renderer->host_wait) {
        wren_wait_idle(queue);
    }

    // Send frame callbacks

    auto elapsed = wroc_get_elapsed_milliseconds();

    for (wroc_surface* surface : server->surfaces) {
        if (surface->role == wroc_surface_role::none) continue;

        wroc_output* surface_output = nullptr;
        auto surface_frame = wroc_surface_get_frame(surface);
        auto centroid = surface_frame.origin + surface_frame.extent * 0.5;
        wroc_output_layout_clamp_position(server->output_layout.get(), centroid, &surface_output);

        // Only dispatch frame callbacks for the surface's primary output
        if (surface_output != output) continue;

        while (auto* callback = surface->current.frame_callbacks.front()) {
            // log_trace("Sending frame callback: {}", (void*)callback);
            wroc_send(wl_callback_send_done, callback, elapsed);
            wl_resource_destroy(callback);
        }
    }
}
