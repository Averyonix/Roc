#include "server.hpp"

#include "wrei/object.hpp"

#include "wren/wren.hpp"
#include "wren/wren_internal.hpp"

#include "wroc_blit_shader.hpp"
#include "shaders/blit.h"

#include "wroc/event.hpp"

static
constexpr u32 wroc_max_rects = 65'536;

void wroc_renderer_create(wroc_server* server, wroc_render_options render_options)
{

    auto* renderer = (server->renderer = wrei_create<wroc_renderer>()).get();
    renderer->server = server;
    renderer->options = render_options;

    wren_features features = {};
    if (!(render_options >= wroc_render_options::no_dmabuf)) {
        features |= wren_features::dmabuf;
    }

    renderer->wren = wren_create(features);
    wroc_renderer_init_buffer_feedback(renderer);

    auto* wren = renderer->wren.get();

    std::filesystem::path path = getenv("WALLPAPER");

    int w, h;
    int num_channels;
    stbi_uc* data = stbi_load(path.c_str(), &w, &h, &num_channels, STBI_rgb_alpha);
    defer { stbi_image_free(data); };

    log_info("Loaded image ({}, width = {}, height = {})", path.c_str(), w, h);

    renderer->background = wren_image_create(wren, {w, h}, wren_format_from_drm(DRM_FORMAT_ABGR8888));
    wren_image_update(renderer->background.get(), data);

    renderer->sampler = wren_sampler_create(wren, VK_FILTER_NEAREST, VK_FILTER_LINEAR);

    renderer->pipeline = wren_pipeline_create(wren,
        wren_blend_mode::premultiplied, server->renderer->output_format,
        wroc_blit_shader, "vertex", "fragment");

    renderer->rects = {wren_buffer_create(wren, wroc_max_rects * sizeof(wroc_shader_rect))};
}

void wroc_render_frame(wroc_output* output)
{
    auto* server = output->server;
    auto* renderer = server->renderer.get();
    auto* wren = renderer->wren.get();
    auto cmd = wren_begin_commands(wren);

    auto current = wroc_output_acquire_image(output);
    auto current_extent = vec2f32(current.extent.width, current.extent.height);

    wren_transition(wren, cmd, current.image,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        0, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    wren->vk.CmdClearColorImage(cmd, current.image,
        VK_IMAGE_LAYOUT_GENERAL,
        wrei_ptr_to(VkClearColorValue{.float32{0.f, 0.f, 0.f, 1.f}}),
        1, wrei_ptr_to(VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}));

    wren->vk.CmdBeginRendering(cmd, wrei_ptr_to(VkRenderingInfo {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { {}, current.extent },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = wrei_ptr_to(VkRenderingAttachmentInfo {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = current.view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        }),
    }));

    wren->vk.CmdSetViewport(cmd, 0, 1, wrei_ptr_to(VkViewport {
        0, 0,
        current_extent.x, current_extent.y,
        0, 1,
    }));
    wren->vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, wren->pipeline_layout, 0, 1, &wren->set, 0, nullptr);

    auto start_draws = [&] {
        wren->vk.CmdSetScissor(cmd, 0, 1, wrei_ptr_to(VkRect2D { {}, current.extent }));
        wren->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->pipeline->pipeline);
    };

    u32 rect_id_start = 0;
    u32 rect_id = 0;

    auto draw = [&](wren_image* image, vec2f32 offset, vec2f32 extent) {
        assert(rect_id < wroc_max_rects);

        // TODO: Async buffer waits
        // wren_image_wait(image);

        renderer->rects[rect_id++] = wroc_shader_rect {
            .image = image4f32{image, renderer->sampler.get()},
            .image_rect = { {}, image->extent },
            .image_has_alpha = image->format->has_alpha,
            .rect = { offset, extent },
            .opacity = 1.f,
        };
    };

    auto flush_draws = [&] {
        if (rect_id_start == rect_id) return;

        wroc_shader_rect_input si = {};
        si.rects = renderer->rects.device();
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

    draw(output->server->renderer->background.get(), {}, current_extent);

    auto draw_surface = [&](this auto&& draw_surface, wroc_surface* surface, vec2i32 pos) -> void {
        if (!surface) return;

        // log_trace("drawing surface: {} (stack.size = {})", (void*)surface, surface->current.surface_stack.size());

        auto* buffer = surface->current.buffer.get();
        if (!buffer || !buffer->image) return;

        for (auto& s : surface->current.surface_stack) {
            if (s.get() == surface) {
                s->position = pos;

                // Draw self
                draw(buffer->image.get(), pos, buffer->extent);

            } else if (auto* subsurface = wroc_surface_get_addon<wroc_subsurface>(s.get())) {

                // Draw subsurface
                draw_surface(s.get(), pos + subsurface->current.position);
            }
        }
    };

    // Draw xdg surfaces

    for (auto* surface : output->server->surfaces) {
        auto* xdg_surface = wroc_surface_get_addon<wroc_xdg_surface>(surface);
        if (!xdg_surface) continue;
        auto pos = wroc_xdg_surface_get_position(xdg_surface);

        // log_debug("Drawing toplevel");
        draw_surface(surface, pos);
    }

    // Draw ImGui

    bool imgui_wants_mouse = false;
    if (server->imgui) {
        flush_draws();
        wroc_imgui_frame(server->imgui.get(), current_extent, cmd, &imgui_wants_mouse);
        start_draws();
    }

    // Draw cursor

    if (server->data_manager.drag.source) {
        if (auto* icon = server->data_manager.drag.icon.get()) {
            if (auto* buffer = icon->surface->current.buffer.get()) {
                if (buffer->image) {
                    draw(buffer->image.get(),
                        glm::ceil(server->seat->pointer->layout_position) + vec2f64(icon->offset),
                        buffer->extent);
                }
            }
        }
    }

    if (!imgui_wants_mouse) {
        if (auto* pointer = server->seat->pointer) {
            // TODO: Move this to cursor.cpp
            if (pointer->focused_surface && pointer->focused_surface->cursor) {
                // If surface is focused and has cursor set, render cursor surface (possibly hidden)
                if (auto* cursor_surface = pointer->focused_surface->cursor->get()) {
                    auto pos = vec2i32(pointer->layout_position) - cursor_surface->hotspot;
                    draw_surface(cursor_surface->surface.get(), pos);
                }
            } else {
                // ... else fall back to default cursor
                auto* cursor = server->cursor.get();
                auto& fallback = cursor->fallback;
                auto pos = vec2i32(pointer->layout_position) - fallback.hotspot;
                draw(fallback.image.get(), pos, fallback.image->extent);
            }
        }
    }

    // Finish

    flush_draws();

    wren->vk.CmdEndRendering(cmd);

    // Compute toplevel-under-cursor
    // TODO: Derive this from render readback

    auto surface_accepts_input = [&](this auto&& surface_accepts_input, wroc_surface* surface, vec2i32 surface_pos, vec2f64 cursor_pos) -> wroc_surface* {
        if (!surface) return nullptr;
        if (!surface->current.buffer) return nullptr;
        for (auto& s : surface->current.surface_stack | std::views::reverse) {
            if (s.get() == surface) {
                if (wroc_surface_point_accepts_input(s.get(), cursor_pos - vec2f64(surface_pos))) {
                    return s.get();
                }
            } else if (auto* subsurface = wroc_surface_get_addon<wroc_subsurface>(s.get())) {
                if (auto* under = surface_accepts_input(s.get(), surface_pos + subsurface->current.position, cursor_pos)) {
                    return under;
                }
            }
        }
        return nullptr;
    };

    output->server->toplevel_under_cursor = {};
    output->server->surface_under_cursor = {};
    if (!imgui_wants_mouse) {
        if (auto* pointer = output->server->seat->pointer) {
            for (auto* surface : output->server->surfaces | std::views::reverse) {
                if (auto* toplevel = wroc_surface_get_addon<wroc_toplevel>(surface)) {
                    auto surface_pos = wroc_xdg_surface_get_position(toplevel->base());
                    if (auto* surface_under_cursor = surface_accepts_input(surface, surface_pos, pointer->layout_position)) {
                        output->server->toplevel_under_cursor = toplevel;
                        output->server->surface_under_cursor = surface_under_cursor;
                        break;
                    }
                }
                if (auto* popup = wroc_surface_get_addon<wroc_popup>(surface)) {
                    auto surface_pos = wroc_xdg_surface_get_position(popup->base());
                    if (auto* surface_under_cursor = surface_accepts_input(surface, surface_pos, pointer->layout_position)) {
                        output->server->toplevel_under_cursor = popup->root_toplevel;
                        output->server->surface_under_cursor = surface_under_cursor;
                        break;
                    }
                }
            }
        }
    }

    if (renderer->screenshot_queued) {
        renderer->screenshot_queued = false;

        // TODO: Factor this out to function and add options (rect, format, etc..)
        // TODO: Also just render everything twice instead of copying?

        wren_submit_commands(wren, cmd);

        auto row_stride = current.extent.width * 4;
        auto byte_size = row_stride * current.extent.height;

        log_warn("Saving screen ({}, {})", current.extent.width, current.extent.height);
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

    wren_transition(wren, cmd, current.image,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, 0,
        VK_ACCESS_2_TRANSFER_WRITE_BIT, 0,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    wren_submit_commands(wren, cmd);

    wren_check(vkwsi_swapchain_present(&output->swapchain, 1, wren->queue, nullptr, 0, false));

    // Send frame callbacks

    auto elapsed = wroc_get_elapsed_milliseconds(output->server);

    for (wroc_surface* surface : output->server->surfaces) {
        while (auto* callback = surface->current.frame_callbacks.front()) {
            // log_trace("Sending frame callback: {}", (void*)callback);
            wl_callback_send_done(callback, elapsed);
            wl_resource_destroy(callback);
        }
    }
}
