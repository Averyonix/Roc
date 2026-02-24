#include "internal.hpp"

#include "render.h"
#include "wrui_render_shader.hpp"

#include "gpu/internal.hpp"

void wrui_render_init(wrui_context* ctx)
{
    ctx->render.usage = wren_image_usage::render;

    ctx->render.postmult = wren_pipeline_create_graphics(
        ctx->wren, wren_blend_mode::postmultiplied,
        wren_format_from_drm(DRM_FORMAT_ABGR8888),
        wrui_render_shader, "vertex", "fragment");

    ctx->render.premult = wren_pipeline_create_graphics(
        ctx->wren, wren_blend_mode::premultiplied,
        wren_format_from_drm(DRM_FORMAT_ABGR8888),
        wrui_render_shader, "vertex", "fragment");

    ctx->render.white = wren_image_create(ctx->wren, {1, 1},
        wren_format_from_drm(DRM_FORMAT_ABGR8888),
        wren_image_usage::texture | wren_image_usage::transfer_dst);
    wren_image_update_immed(ctx->render.white.get(), wrei_ptr_to(vec4u8{255, 255, 255, 255}));

    ctx->render.sampler = wren_sampler_create(ctx->wren, VK_FILTER_NEAREST, VK_FILTER_LINEAR);
}

void wrui_render(wrui_context* ctx, wrio_output* output, wren_image* target)
{
    auto& render = ctx->render;

    static u32 captures = 3;
    bool capture = ctx->wren->renderdoc && captures;
    if (capture) {
        captures--;
        ctx->wren->renderdoc->StartFrameCapture(nullptr, nullptr);
        ctx->wren->renderdoc->SetCaptureTitle(std::format("Roc capture {}", 3 - captures).c_str());
    }

    struct wrui_draw
    {
        u32 first_vertex;
        u32 first_index;
        u32 num_indices;
        aabb2f32 clip;
        wren_image* image;
        wren_sampler* sampler;
        wren_blend_mode blend;
        wrui_transform_state transform;
    };

    std::vector<wrui_vertex> vertices;
    std::vector<u32> indices;
    std::vector<wrui_draw> draws;

    aabb2f32 default_clip = {{}, target->extent, wrei_xywh};

    auto walk_node = [&](this auto&& walk_node, wrui_node* node) -> void
    {
        switch (node->type) {
            break;case wrui_node_type::transform:
                wrei_assert_fail("", "Unexpected transform node in layer stack");
            break;case wrui_node_type::tree: {
                for (auto& child : static_cast<wrui_tree*>(node)->children) {
                    walk_node(child.get());
                }
            }
            break;case wrui_node_type::mesh: {
                auto* mesh = static_cast<wrui_mesh*>(node);

                u32 vtx = vertices.size();
                u32 idx = indices.size();
                vertices.insert_range(vertices.end(), mesh->vertices);
                indices.insert_range(indices.end(), mesh->indices);

                draws.emplace_back(wrui_draw {
                    .first_vertex = vtx,
                    .first_index = idx,
                    .num_indices = u32(mesh->indices.size()),
                    .clip = wrei_aabb_inner(default_clip, {
                        node->transform->global.translation + mesh->clip.min * node->transform->global.scale,
                        node->transform->global.translation + mesh->clip.max * node->transform->global.scale,
                        wrei_minmax
                    }),
                    .image = mesh->image.get() ?: render.white.get(),
                    .sampler = mesh->sampler.get() ?: render.sampler.get(),
                    .blend = mesh->blend,
                    .transform = mesh->transform->global,
                });
            }
            break;case wrui_node_type::texture: {
                auto* texture = static_cast<wrui_texture*>(node);
                aabb2f32 src = texture->src;
                aabb2f32 dst = texture->dst;

                //  0 ---- 1
                //  | a /  |  a = 0,2,1
                //  |  / b |  b = 1,2,3
                //  2 ---- 3

                u32 vtx = vertices.size();
                u32 idx = indices.size();
                auto push_vtx = [&](vec2f32 pos, vec2f32 uv = {}) {
                    vertices.emplace_back(wrui_vertex { .pos = pos, .uv = uv, .color = texture->tint });
                };
                push_vtx({dst.min.x, dst.min.y}, {src.min.x, src.min.y});
                push_vtx({dst.max.x, dst.min.y}, {src.max.x, src.min.y});
                push_vtx({dst.min.x, dst.max.y}, {src.min.x, src.max.y});
                push_vtx({dst.max.x, dst.max.y}, {src.max.x, src.max.y});
                indices.append_range(std::array{0, 2, 1, 1, 2, 3});

                draws.emplace_back(wrui_draw {
                    .first_vertex = vtx,
                    .first_index = idx,
                    .num_indices = 6,
                    .clip = default_clip,
                    .image = texture->image.get() ?: render.white.get(),
                    .sampler = texture->sampler.get() ?: render.sampler.get(),
                    .blend = texture->blend,
                    .transform = texture->transform->global,
                });
            }
            break;case wrui_node_type::input_plane:
                ;
        }
    };

    walk_node(ctx->root_tree.get());

    auto wren = ctx->wren;

    auto make_gpu = [&]<typename T>(std::span<T> data) {
        wren_array<T> arr{wren_buffer_create(wren, data.size_bytes(), {}), 0};
        std::memcpy(arr.host(), data.data(), data.size_bytes());
        return arr;
    };

    auto gpu_vertices = make_gpu(std::span(vertices));
    auto gpu_indices  = make_gpu(std::span(indices));

    auto queue = wren_get_queue(wren, wren_queue_type::graphics);
    auto commands = wren_commands_begin(queue);
    auto cmd = commands->buffer;
    wren_commands_protect_object(commands.get(), gpu_vertices.buffer.get());
    wren_commands_protect_object(commands.get(), gpu_indices.buffer.get());

    // Protect images

    wren_commands_protect_object(commands.get(), render.white.get());
    for (auto& draw : draws) {
        wren_commands_protect_object(commands.get(), draw.image);
    }

    // Record

    VkExtent2D vk_extent = { target->extent.x, target->extent.y };
    vec2f32 target_extent = target->extent;

    wren->vk.CmdBeginRendering(cmd, wrei_ptr_to(VkRenderingInfo {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { {}, vk_extent },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = wrei_ptr_to(VkRenderingAttachmentInfo {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = target->view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.color{.float32{0.f, 0.f, 0.f, 1.f}}},
        }),
    }));
    wren->vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, wren->pipeline_layout, 0, 1, &wren->set, 0, nullptr);
    wren->vk.CmdBindIndexBuffer(cmd, gpu_indices.buffer->buffer, 0, VK_INDEX_TYPE_UINT32);

    wren->vk.CmdSetViewport(cmd, 0, 1, wrei_ptr_to(VkViewport {
        0, 0,
        target_extent.x, target_extent.y,
        0, 1,
    }));

    rect2f32 viewport{{}, target_extent, wrei_xywh};

    for (auto& draw : draws) {
        switch (draw.blend) {
            break;case wren_blend_mode::premultiplied:
                wren->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, render.premult->pipeline);
            break;case wren_blend_mode::postmultiplied:
                wren->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, render.postmult->pipeline);
            break;case wren_blend_mode::none:
                wrei_assert_fail("", "Must select blend mode");
        }
        rect2f32 scissor = draw.clip;
        wren->vk.CmdSetScissor(cmd, 0, 1, wrei_ptr_to(VkRect2D {
            .offset = {i32(scissor.origin.x), i32(scissor.origin.y)},
            .extent = {u32(scissor.extent.x), u32(scissor.extent.y)},
        }));
        auto draw_scale = 2.f / viewport.extent;
        wren->vk.CmdPushConstants(cmd, wren->pipeline_layout, VK_SHADER_STAGE_ALL, 0, sizeof(wrui_render_input),
            wrei_ptr_to(wrui_render_input {
                .vertices = gpu_vertices.device(),
                .scale = draw_scale * draw.transform.scale,
                .offset = (draw.transform.translation - viewport.origin) * draw_scale - 1.f,
                .texture = {draw.image, render.sampler.get()},
            }));
        wren->vk.CmdDrawIndexed(cmd, draw.num_indices, 1, draw.first_index, draw.first_vertex, 0);
    }

    wren->vk.CmdEndRendering(cmd);

    auto done = wren_commands_submit(commands.get(), {});

    if (capture) {
        ctx->wren->renderdoc->EndFrameCapture(nullptr, nullptr);
    }

    wrio_output_present(output, target, done);
}
