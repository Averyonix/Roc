#include "server.hpp"
#include "wren/wren_internal.hpp"

#include "wroc_imgui_shader.hpp"
#include "shaders/imgui.h"

static constexpr u32 wroc_imgui_max_indices  = 1 << 18;
static constexpr u32 wroc_imgui_max_vertices = 1 << 18;

void wroc_imgui_init(wroc_server* server)
{
    auto* wren = server->renderer->wren.get();

    auto* imgui = wrei_get_registry(server)->create<wroc_imgui>();
    server->imgui = wrei_adopt_ref(imgui);
    imgui->server = server;

    imgui->indices  = {wren_buffer_create(wren, wroc_imgui_max_indices  * sizeof(ImDrawIdx))};
    imgui->vertices = {wren_buffer_create(wren, wroc_imgui_max_vertices * sizeof(ImDrawVert))};

    imgui->pipeline = wren_pipeline_create(wren,
        wren_blend_mode::postmultiplied, server->renderer->output_format,
        wroc_imgui_shader, "vertex", "fragment");

    imgui->context = ImGui::CreateContext();
    ImGui::SetCurrentContext(imgui->context);

    {
        unsigned char* pixels;
        int width, height;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

        imgui->font_image = wren_image_create(wren, {width, height}, wren_format_from_drm(DRM_FORMAT_ABGR8888));
        wren_image_update(imgui->font_image.get(), pixels);

        ImGui::GetIO().Fonts->SetTexID(wroc_imgui_texture(imgui->font_image.get(), server->renderer->sampler.get()));
    }
}

void wroc_imgui_frame(wroc_imgui* imgui, vec2u32 extent, VkCommandBuffer cmd)
{
    auto* wren = imgui->server->renderer->wren.get();

    {
        auto& io = ImGui::GetIO();

        io.DisplaySize = ImVec2(extent.x, extent.y);

        auto now = std::chrono::steady_clock::now();
        if (imgui->last_frame != std::chrono::steady_clock::time_point{}) {
            io.DeltaTime = std::min(std::chrono::duration_cast<std::chrono::duration<f32>>(now - imgui->last_frame).count(), 1.f);
        }
        imgui->last_frame = now;
    }

    ImGui::NewFrame();

    ImGui::ShowDemoWindow();

    ImGui::Render();

    auto data = ImGui::GetDrawData();
    if (!data->TotalIdxCount) return;

    wren->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, imgui->pipeline->pipeline);
    wren->vk.CmdBindIndexBuffer(cmd,
        imgui->indices.buffer->buffer, imgui->indices.byte_offset,
        sizeof(ImDrawIdx) == sizeof(u16) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);

    vec2f32 clip_offset = { data->DisplayPos.x, data->DisplayPos.y };
    vec2f32 clip_scale  = { data->FramebufferScale.x, data->FramebufferScale.y };

    u32 vertex_offset = 0;
    u32 index_offset = 0;
    for (i32 i = 0; i < data->CmdListsCount; ++i) {
        auto list = data->CmdLists[i];

        assert(vertex_offset + list->VtxBuffer.size() <= wroc_imgui_max_vertices);
        assert(index_offset + list->IdxBuffer.size() <= wroc_imgui_max_indices);

        std::memcpy(imgui->vertices.host() + vertex_offset, list->VtxBuffer.Data, list->VtxBuffer.size() * sizeof(ImDrawVert));
        std::memcpy(imgui->indices.host()  + index_offset,  list->IdxBuffer.Data, list->IdxBuffer.size() * sizeof(ImDrawIdx));

        for (i32 j = 0; j < list->CmdBuffer.size(); ++j) {
            const auto& im_cmd = list->CmdBuffer[j];

            assert(!im_cmd.UserCallback);

            auto clip_min = glm::max((vec2f32(im_cmd.ClipRect.x, im_cmd.ClipRect.y) - clip_offset) * clip_scale, {});
            auto clip_max = glm::min((vec2f32(im_cmd.ClipRect.z, im_cmd.ClipRect.w) - clip_scale), vec2f32(extent));
            if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y) {
                continue;
            }

            wren->vk.CmdSetScissor(cmd, 0, 1, wrei_ptr_to(VkRect2D {
                .offset = {i32(clip_min.x), i32(clip_min.y)},
                .extent = {u32(clip_max.x - clip_min.x), u32(clip_max.y - clip_min.y)},
            }));
            wren->vk.CmdPushConstants(cmd, wren->pipeline_layout, VK_SHADER_STAGE_ALL, 0, sizeof(wroc_imgui_shader_in),
                wrei_ptr_to(wroc_imgui_shader_in {
                    .vertices = imgui->vertices.device(),
                    .scale = 2.f / vec2f32(extent),
                    .offset = vec2f32(-1.f),
                    .texture = std::bit_cast<wroc_imgui_texture>(im_cmd.GetTexID()).handle,
                }));
            wren->vk.CmdDrawIndexed(cmd, im_cmd.ElemCount, 1, index_offset + im_cmd.IdxOffset, vertex_offset + im_cmd.VtxOffset, 0);
        }

        vertex_offset += list->VtxBuffer.size();
        index_offset += list->IdxBuffer.size();
    }
}
