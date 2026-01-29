#include "wroc.hpp"

#include "wrei/object.hpp"

#include "wren/wren.hpp"

#include "wroc_blit_shader.hpp"
#include "shaders/blit.h"

#include "wroc/event.hpp"

wroc_renderer::~wroc_renderer() = default;

static
void register_format(wroc_renderer* renderer, wren_format format)
{
    auto wren = renderer->wren.get();

    if (!format->is_ycbcr) {
        if (wren_get_format_props(wren, format, wren_image_usage::texture | wren_image_usage::transfer)->opt_props.get()) {
            renderer->shm_formats.add(format, DRM_FORMAT_MOD_LINEAR);
        }
    }

    for (auto& props : wren_get_format_props(wren, format, wren_image_usage::texture | wren_image_usage::transfer_src)->mod_props) {
        if (props.ext_mem_props.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) {
            renderer->dmabuf_formats.add(format, props.modifier);
        }
    }
}

void wroc_renderer_create(wroc_render_options render_options)
{
    auto* renderer = (server->renderer = wrei_create<wroc_renderer>()).get();
    renderer->options = render_options;

    wren_features features = {};

    renderer->wren = wren_create(features, server->event_loop.get(), server->backend->get_preferred_drm_device());

    for (auto& format : wren_get_formats()) {
        register_format(renderer, &format);
    }

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

void render(wroc_renderer* renderer, wren_commands* commands, wroc_renderer_frame_data* frame, wren_image* current, rect2f64 scene_rect)
{
    auto* wren = commands->queue->ctx;
    auto cmd = commands->buffer;

    wroc_coord_space space {
        .origin = scene_rect.origin,
        .scale = scene_rect.extent / vec2f64(current->extent),
    };

    VkExtent2D vk_extent = { current->extent.x, current->extent.y };
    vec2f32 current_extent = current->extent;

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

        // log_debug("draw(dest = {}, source = {})", wrei_to_string(dest), wrei_to_string(source));

        wren_commands_protect_object(commands, image);

        auto pixel_dst = wrei_round<i32, f64>(space.from_global(dest));

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
                wren_commands_protect_object(commands, frame->rects.buffer.get());
            }
            frame->rects = {wren_buffer_create(wren, new_size * sizeof(wroc_shader_rect), {}), new_size};
        }

        if (!renderer->rects_cpu.empty()) {
            std::memcpy(frame->rects.host() + rect_id_start, renderer->rects_cpu.data() + rect_id_start, (rect_id - rect_id_start) * sizeof(wroc_shader_rect));
        }

        wroc_shader_rect_input si = {};
        si.rects = frame->rects.device();
        si.output_size = current_extent;
        wren->vk.CmdPushConstants(cmd, wren->pipeline_layout, VK_SHADER_STAGE_ALL, 0, sizeof(si), &si);
        wren->vk.CmdDraw(cmd, 6 * (rect_id - rect_id_start), 1, rect_id_start * 6, 0);
        rect_id_start = rect_id;
    };

    start_draws();

    // Background

    for (auto& output : server->output_layout->outputs) {
        auto src = wrei_rect_fit(server->renderer->background->extent, output->layout_rect.extent);
        draw(server->renderer->background.get(), output->layout_rect, src);
    }

    // Surface helper

    auto draw_surface = [&](this auto&& draw_surface, wroc_surface* surface, vec2f64 pos, vec2f64 scale, f64 opacity = 1.0) -> void {
        if (!surface || !surface->mapped) return;

        // log_trace("drawing surface: {} (stack.size = {})", (void*)surface, surface->current.surface_stack.size());

        for (auto&[s, s_pos] : surface->current.surface_stack) {
            if (s.get() == surface) {

                // Draw self
                rect2f64 dst = surface->buffer_dst;
                dst.origin *= scale;
                dst.extent *= scale;
                dst.origin += pos;

                draw(surface->current.buffer->image.get(), dst, surface->buffer_src, vec4f32(opacity, opacity, opacity, opacity));

            } else {

                // Draw subsurface
                draw_surface(s.get(), pos + vec2f64(s_pos) * scale, scale, opacity);
            }
        }
    };

    // Draw xdg surfaces

    auto draw_xdg_surfaces = [&](bool show_cycled) {
        for (auto* surface : server->surfaces) {
            if (!surface->mapped) continue;

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

    {
        flush_draws();
        wroc_imgui_render(server->imgui.get(), commands, scene_rect, current->extent);
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

    {
        auto* pointer = server->seat->pointer.get();

        auto* surface = server->imgui->wants_mouse
            ? wroc_cursor_get_shape(server->cursor.get(), server->imgui->cursor_shape)
            : wroc_cursor_get_current(pointer, server->cursor.get());
        if (surface) {
            draw_surface(surface, pointer->position, vec2f64(1.0));
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
}

static
void on_screenshot_ready(std::chrono::steady_clock::time_point start, wren_image* image, wren_buffer* buffer)
{
    auto save_start = std::chrono::steady_clock::now();
    log_info("Screenshot captured in {}, saving...", wrei_duration_to_string(save_start - start));

    auto save_path = "screenshot.png";

    stbi_write_png(save_path, image->extent.x, image->extent.y, STBI_rgb_alpha, buffer->host_address, image->extent.x * 4);
    auto save_end = std::chrono::steady_clock::now();
    log_info("Screenshot saved in {}", wrei_duration_to_string(save_end - save_start));

    wrei_event_loop_enqueue(server->event_loop.get(), [image, buffer] {
        log_debug("Deleting screenshot resources");
        server->renderer->screenshot_queued = false;
        wrei_remove_ref(image);
        wrei_remove_ref(buffer);
    });
}

void wroc_screenshot(rect2f64 rect)
{
    auto* renderer = server->renderer.get();
    auto* wren = renderer->wren.get();

    log_info("Taking screenshot of region {}", wrei_to_string(rect));

    auto start = std::chrono::steady_clock::now();

    vec2u32 extent = rect.extent;
    auto image = wren_image_create(wren, extent,
        wren_format_from_drm(DRM_FORMAT_ABGR8888),
        wren_image_usage::render | wren_image_usage::transfer);

    auto byte_size = usz(4) * extent.x * extent.y;
    auto buffer = wren_buffer_create(wren, byte_size, wren_buffer_flags::host);

    // Completion handler

    struct screenshot_guard : wrei_object
    {
        wroc_renderer_frame_data frame = {};
        std::chrono::steady_clock::time_point start;
        ref<wren_image> image;
        ref<wren_buffer> buffer;

        ~screenshot_guard()
        {
            std::thread{on_screenshot_ready, start, wrei_add_ref(image.get()), wrei_add_ref(buffer.get())}.detach();
        }
    };
    auto guard = wrei_create<screenshot_guard>();
    guard->start = start;
    guard->image = image;
    guard->buffer = buffer;

    // Render

    auto render_queue = wren_get_queue(wren, wren_queue_type::graphics);
    auto render_commands = wren_commands_begin(render_queue);
    render(renderer, render_commands.get(), &guard->frame, image.get(), rect);
    wren_commands_protect_object(render_commands.get(), guard.get());
    auto render_done = wren_commands_submit(render_commands.get(), {});

    // Transfer

    auto transfer_queue = wren_get_queue(image->ctx, wren_queue_type::transfer);
    auto transfer_commands = wren_commands_begin(transfer_queue);
    wren_copy_image_to_buffer(transfer_commands.get(), buffer.get(), image.get());
    wren_commands_protect_object(transfer_commands.get(), guard.get());
    wren_commands_submit(transfer_commands.get(), {render_done});
}

static
ref<wren_image> acquire(wroc_output* output)
{
    for (auto& slot : output->release_slots) {
        if (slot.image && wren_semaphore_get_value(slot.semaphore.get()) >= slot.release_point) {
            auto image = std::exchange(slot.image, nullptr);
            if (image->extent == output->size) return image;
            else log_warn("Discarding out-of-date swapchain image {}", wrei_to_string(image->extent));
        }
    }

    log_warn("Creating new swapchain image {}", wrei_to_string(output->size));

    auto* wren = server->renderer->wren.get();

    auto format = wren_format_from_drm(DRM_FORMAT_ARGB8888);
    auto usage = wren_image_usage::render;
    auto* format_props = wren_get_format_props(wren, format, usage);
    auto image = wren_image_create_dmabuf(wren, output->size, format, usage, format_props->mods);

    return image;
}

static
void present(wroc_output* output, wren_image* image, wren_syncpoint acquire)
{
    auto slot = std::ranges::find_if(output->release_slots, [](const auto& s) { return !s.image; });

    if (slot == output->release_slots.end()) {
        slot = output->release_slots.insert(output->release_slots.end(), wroc_output::release_slot {
            .semaphore = wren_semaphore_create(server->renderer->wren.get()),
        });
    }

    slot->image = image;
    slot->release_point++;

    output->commit(image, acquire, {slot->semaphore.get(), slot->release_point});
}

void wroc_render_frame(wroc_output* output)
{
    if (!output->frame_requested && server->renderer->vsync) return;
    if (output->frames_in_flight >= server->renderer->max_frames_in_flight) return;

    auto current = acquire(output);

    output->frame_requested = false;

    auto* renderer = server->renderer.get();
    auto* wren = renderer->wren.get();

    for (auto* surface : server->surfaces) {
        wroc_surface_flush_apply(surface);
    }

    output->frames_in_flight++;

    auto queue = wren_get_queue(wren, wren_queue_type::graphics);
    auto commands = wren_commands_begin(queue);

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
        log_debug("Allocating new renderer frame data");
    }

    // TODO: We should update imgui frames based on events, instead of this hack
    if (output == server->output_layout->outputs[0].get()) {
        wroc_imgui_frame(server->imgui.get(), output->layout_rect);
    }

    // Prepare

    // TODO: QFOTs for DMABUF swapchains

    render(renderer, commands.get(), frame, current.get(), output->layout_rect);

    // Submit

    auto render_done = wren_commands_submit(commands.get(), {});

    // Present

    present(output, current.get(), render_done);

    if (renderer->host_wait) {
        wren_semaphore_wait_value(render_done.semaphore, render_done.value);
    }

    // Send frame callbacks

    auto elapsed = wroc_get_elapsed_milliseconds();

    for (wroc_surface* surface : server->surfaces) {
        if (surface->role == wroc_surface_role::none) continue;

        auto surface_output = wroc_output_layout_output_for_surface(server->output_layout.get(), surface);

        // Only dispatch frame callbacks for the surface's primary output
        if (surface_output != output) continue;

        auto dispatch = [&](wroc_resource_list& callbacks) {
            while (auto* callback = callbacks.front()) {
                // log_trace("Sending frame callback: {}", (void*)callback);
                wroc_send(wl_callback_send_done, callback, elapsed);
                wl_resource_destroy(callback);
            }
        };

        // Frame callbacks are a special case of surface state.
        // We want to dispatch *all* committed frame callbacks,
        // even if the frame hasn't been rendered yet, to ensure
        // smooth frame pacing even if a buffer is delayed.
        dispatch(surface->current.frame_callbacks);
        for (auto& packet : surface->cached) {
            if (packet.id) {
                if (renderer->noisy_stutters && packet.state.frame_callbacks.front()) {
                    log_warn("Stutter detected - dispatching early frame callbacks for commit {}", packet.id);
                }
                dispatch(packet.state.frame_callbacks);
            }
        }
    }
}
