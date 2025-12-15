#include "server.hpp"

#include "wrei/ref.hpp"

#include "wren/wren.hpp"
#include "wren/wren_internal.hpp"

#include "wroc_shaders.hpp"
#include "shaders/blit.h"

#include "wroc/event.hpp"

static
constexpr u32 wroc_max_rects = 65'536;

static
VkPipeline wroc_renderer_create_pipeline(wroc_renderer* renderer, std::span<const u32> spirv, const char* vertex_entry,  const char* fragment_entry)
{
    auto* wren = renderer->wren.get();
    auto& vk = wren->vk;

    constexpr static bool premultiplied_alpha = true;

    VkPipeline pipeline = {};
    wren_check(vk.CreateGraphicsPipelines(wren->device, nullptr, 1, wrei_ptr_to(VkGraphicsPipelineCreateInfo {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = wrei_ptr_to(VkPipelineRenderingCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = wrei_ptr_to(renderer->output_format),
        }),
        .stageCount = 2,
        .pStages = std::array {
            VkPipelineShaderStageCreateInfo {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = wrei_ptr_to(VkShaderModuleCreateInfo {
                    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                    .codeSize = spirv.size_bytes(),
                    .pCode = spirv.data(),
                }),
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .pName = vertex_entry,
            },
            VkPipelineShaderStageCreateInfo {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = wrei_ptr_to(VkShaderModuleCreateInfo {
                    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                    .codeSize = spirv.size_bytes(),
                    .pCode = spirv.data(),
                }),
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .pName = fragment_entry,
            },
        }.data(),
        .pVertexInputState = wrei_ptr_to(VkPipelineVertexInputStateCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        }),
        .pInputAssemblyState = wrei_ptr_to(VkPipelineInputAssemblyStateCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        }),
        .pViewportState = wrei_ptr_to(VkPipelineViewportStateCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1,
        }),
        .pRasterizationState = wrei_ptr_to(VkPipelineRasterizationStateCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .lineWidth = 1.f,
        }),
        .pMultisampleState = wrei_ptr_to(VkPipelineMultisampleStateCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        }),
        .pDepthStencilState = wrei_ptr_to(VkPipelineDepthStencilStateCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .minDepthBounds = 0,
            .maxDepthBounds = 1,
        }),
        .pColorBlendState = wrei_ptr_to(VkPipelineColorBlendStateCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments = wrei_ptr_to(VkPipelineColorBlendAttachmentState {
                .blendEnable = true,
                .srcColorBlendFactor = premultiplied_alpha
                    ? VK_BLEND_FACTOR_ONE
                    : VK_BLEND_FACTOR_SRC_ALPHA,
                .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                .colorBlendOp = VK_BLEND_OP_ADD,
                .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                .alphaBlendOp = VK_BLEND_OP_ADD,
                .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
            }),
        }),
        .pDynamicState = wrei_ptr_to(VkPipelineDynamicStateCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = 2,
            .pDynamicStates = std::array {
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR,
            }.data(),
        }),
        .layout = wren->pipeline_layout,
    }), nullptr, &pipeline));

    return pipeline;
}

void wroc_renderer_create(wroc_server* server, wroc_render_options render_options)
{
    auto* renderer = (server->renderer = wrei_adopt_ref(wrei_get_registry(server)->create<wroc_renderer>())).get();
    renderer->server = server;
    renderer->options = render_options;

    wren_features features = {};
    if (!(render_options >= wroc_render_options::no_dmabuf)) {
        features |= wren_features::dmabuf;
    }

    renderer->wren = wren_create(wrei_get_registry(server), features);

    std::filesystem::path path = getenv("WALLPAPER");

    int w, h;
    int num_channels;
    stbi_uc* data = stbi_load(path.c_str(), &w, &h, &num_channels, STBI_rgb_alpha);
    defer { stbi_image_free(data); };

    log_info("Loaded image ({}, width = {}, height = {})", path.c_str(), w, h);

    renderer->background = wren_image_create(renderer->wren.get(), {w, h}, VK_FORMAT_R8G8B8A8_UNORM);
    wren_image_update(renderer->background.get(), data);

    renderer->sampler = wren_sampler_create(renderer->wren.get());

    renderer->pipeline = wroc_renderer_create_pipeline(renderer, wroc_blit_shader, "vertex", "fragment");

    renderer->rects = {wren_buffer_create(renderer->wren.get(), wroc_max_rects * sizeof(wroc_shader_rect))};
}

wroc_renderer::~wroc_renderer()
{
    wren->vk.DestroyPipeline(wren->device, pipeline, nullptr);
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
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        }),
    }));

    wren->vk.CmdSetViewport(cmd, 0, 1, wrei_ptr_to(VkViewport {
        0, 0,
        current_extent.x, current_extent.y,
        0, 1,
    }));
    wren->vk.CmdSetScissor(cmd, 0, 1, wrei_ptr_to(VkRect2D { {}, current.extent }));

    wren->vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, wren->pipeline_layout, 0, 1, &wren->set, 0, nullptr);
    wren->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->pipeline);

    u32 rect_id = 0;

    auto draw = [&](wren_image* image, vec2f32 offset, vec2f32 extent) {
        assert(rect_id < wroc_max_rects);

        renderer->rects[rect_id++] = wroc_shader_rect {
            .image = image4f32{image, renderer->sampler.get()},
            .image_rect = { {}, image->extent },
            .rect = { offset, extent },
        };
    };

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

            } else if (auto* subsurface = wroc_subsurface::try_from(s.get())) {

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

    // Draw cursor

    if (auto* pointer = server->seat->pointer) {
        // TODO: Move this to cursor.cpp
        auto* cursor = server->cursor.get();
        if (pointer->focused_surface) {
            if (auto* cursor_surface = cursor->current.get()) {
                auto pos = vec2i32(pointer->layout_position) - cursor_surface->hotspot;
                draw_surface(cursor_surface->surface.get(), pos);
            }
        } else {
            auto& fallback = cursor->fallback;
            auto pos = vec2i32(pointer->layout_position) - fallback.hotspot;
            draw(fallback.image.get(), pos, fallback.image->extent);
        }
    }

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

    // Record draws

    wroc_shader_rect_input si = {};
    si.rects = renderer->rects.device();
    si.output_size = current_extent;
    wren->vk.CmdPushConstants(cmd, wren->pipeline_layout, VK_SHADER_STAGE_ALL, 0, sizeof(si), &si);
    if (renderer->options >= wroc_render_options::separate_draws) {
        for (u32 i = 0; i < rect_id; ++i) {
            wren->vk.CmdDraw(cmd, 6, 1, i * 6, 0);
        }
    } else {
        wren->vk.CmdDraw(cmd, 6 * rect_id, 1, 0, 0);
    }

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
            } else if (auto* subsurface = wroc_subsurface::try_from(s.get())) {
                if (auto* under = surface_accepts_input(s.get(), surface_pos + subsurface->current.position, cursor_pos)) {
                    return under;
                }
            }
        }
        return nullptr;
    };

    output->server->toplevel_under_cursor = {};
    output->server->surface_under_cursor = {};
    if (auto* pointer = output->server->seat->pointer) {
        for (auto* surface : output->server->surfaces | std::views::reverse) {
            if (auto* toplevel = wroc_surface_get_addon<wroc_xdg_toplevel>(surface)) {
                auto surface_pos = wroc_xdg_surface_get_position(toplevel->base.get());
                if (auto* surface_under_cursor = surface_accepts_input(surface, surface_pos, pointer->layout_position)) {
                    output->server->toplevel_under_cursor = toplevel;
                    output->server->surface_under_cursor = surface_under_cursor;
                    break;
                }
            }
            if (auto* popup = wroc_surface_get_addon<wroc_xdg_popup>(surface)) {
                auto surface_pos = wroc_xdg_surface_get_position(popup->base.get());
                if (auto* surface_under_cursor = surface_accepts_input(surface, surface_pos, pointer->layout_position)) {
                    output->server->toplevel_under_cursor = popup->root_toplevel;
                    output->server->surface_under_cursor = surface_under_cursor;
                    break;
                }
            }
        }
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
