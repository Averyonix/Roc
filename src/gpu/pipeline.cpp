#include "internal.hpp"

struct gpu_shader
{
    gpu_context* ctx;

    VkShaderStageFlagBits stage;
    VkShaderEXT shader = {};

    ~gpu_shader();
};

CORE_OBJECT_EXPLICIT_DEFINE(gpu_shader)

gpu_shader::~gpu_shader()
{
    ctx->vk.DestroyShaderEXT(ctx->device, shader, nullptr);
}

auto gpu_shader_create(gpu_context* ctx, VkShaderStageFlagBits stage, std::span<const u32> spirv, const char* entry) -> ref<gpu_shader>
{
    auto shader = core_create<gpu_shader>();
    shader->ctx = ctx;
    shader->stage = stage;

    flags<VkShaderStageFlagBits> next_stages = {};
    switch (stage) {
        break;case VK_SHADER_STAGE_VERTEX_BIT:
            next_stages = VK_SHADER_STAGE_FRAGMENT_BIT;
        break;default:
    }

    gpu_check(ctx->vk.CreateShadersEXT(ctx->device, 1, ptr_to(VkShaderCreateInfoEXT {
        .sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
        .stage = stage,
        .nextStage = next_stages.get(),
        .codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT,
        .codeSize = spirv.size() * 4,
        .pCode = spirv.data(),
        .pName = entry,
        .setLayoutCount = 1,
        .pSetLayouts = &ctx->set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = ptr_to(VkPushConstantRange {
            .stageFlags = VK_SHADER_STAGE_ALL,
            .size = 128,
        }),
    }), nullptr, &shader->shader));

    return shader;
}

void gpu_cmd_push_constants(gpu_commands* cmd, u64 offset, u64 size, const void* data)
{
    auto* ctx = cmd->queue->ctx;
    ctx->vk.CmdPushConstants(cmd->buffer, ctx->pipeline_layout, VK_SHADER_STAGE_ALL, offset, size, data);
}

void gpu_cmd_set_scissors(gpu_commands* cmd, std::span<const rect2i32> scissors)
{
    auto* ctx = cmd->queue->ctx;

    std::vector<VkRect2D> vk_scissors(scissors.size());
    for (u32 i = 0; i < scissors.size(); ++i) {
        auto r = scissors[i];
        if (r.extent.x < 0) { r.origin.x -= (r.extent.x *= -1); }
        if (r.extent.y < 0) { r.origin.y -= (r.extent.y *= -1); }
        vk_scissors[i] = VkRect2D {
            .offset{    r.origin.x,       r.origin.y  },
            .extent{ u32(r.extent.x), u32(r.extent.y) },
        };
    }
    ctx->vk.CmdSetScissorWithCount(cmd->buffer, u32(scissors.size()), vk_scissors.data());
}

void gpu_cmd_set_viewports(gpu_commands* cmd, std::span<const rect2f32> viewports)
{
    auto* ctx = cmd->queue->ctx;

    std::vector<VkViewport> vk_viewports(viewports.size());
    for (u32 i = 0; i < viewports.size(); ++i) {
        vk_viewports[i] = VkViewport {
            .x = viewports[i].origin.x,
            .y = viewports[i].origin.y,
            .width = viewports[i].extent.x,
            .height = viewports[i].extent.y,
            .minDepth = 0.f,
            .maxDepth = 1.f
        };
    }
    ctx->vk.CmdSetViewportWithCount(cmd->buffer, u32(viewports.size()), vk_viewports.data());
}

void gpu_cmd_set_polygon_state(gpu_commands* cmd, VkPrimitiveTopology topology, VkPolygonMode polygon_mode, f32 line_width)
{
    auto* ctx = cmd->queue->ctx;

    ctx->vk.CmdSetPrimitiveTopology(cmd->buffer, topology);
    ctx->vk.CmdSetPolygonModeEXT(   cmd->buffer, polygon_mode);
    ctx->vk.CmdSetLineWidth(        cmd->buffer, line_width);
}

void gpu_cmd_set_cull_state(gpu_commands* cmd, VkCullModeFlagBits cull_mode, VkFrontFace front_face)
{
    auto* ctx = cmd->queue->ctx;

    ctx->vk.CmdSetCullMode( cmd->buffer, cull_mode);
    ctx->vk.CmdSetFrontFace(cmd->buffer, front_face);
}

void gpu_cmd_set_depth_stae(gpu_commands* cmd, bool test_enable, bool write_enable, VkCompareOp compare_op)
{
    auto* ctx = cmd->queue->ctx;

    ctx->vk.CmdSetDepthTestEnable( cmd->buffer, test_enable);
    ctx->vk.CmdSetDepthWriteEnable(cmd->buffer, write_enable);
    ctx->vk.CmdSetDepthCompareOp(  cmd->buffer, compare_op);
}

void gpu_cmd_set_blend_state(gpu_commands* cmd, std::span<const gpu_blend_mode> blends)
{
    auto* ctx = cmd->queue->ctx;

    auto count = u32(blends.size());

    std::vector<VkColorComponentFlags> components(count);
    std::vector<VkBool32> blend_enable_bools(count);
    std::vector<VkColorBlendEquationEXT> blend_equations(count);

    for (u32 i = 0; i < count; ++i) {
        components[i] = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
            | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blend_enable_bools[i] = blends[i] != gpu_blend_mode::none;

        switch (blends[i]) {
            break;case gpu_blend_mode::none:
                blend_equations[i] = {
                    .srcColorBlendFactor = VK_BLEND_FACTOR_ZERO,
                    .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
                    .colorBlendOp = VK_BLEND_OP_ADD,
                    .srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
                    .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
                    .alphaBlendOp = VK_BLEND_OP_ADD,
                };
            break;case gpu_blend_mode::postmultiplied:
                blend_equations[i] = {
                    .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
                    .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                    .colorBlendOp = VK_BLEND_OP_ADD,
                    .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                    .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                    .alphaBlendOp = VK_BLEND_OP_ADD,
                };
            break;case gpu_blend_mode::premultiplied:
                blend_equations[i] = {
                    .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
                    .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                    .colorBlendOp = VK_BLEND_OP_ADD,
                    .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                    .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                    .alphaBlendOp = VK_BLEND_OP_ADD,
                };
        }
    }

    ctx->vk.CmdSetColorBlendEnableEXT(  cmd->buffer, 0, count, blend_enable_bools.data());
    ctx->vk.CmdSetColorWriteMaskEXT(    cmd->buffer, 0, count, components.data());
    ctx->vk.CmdSetColorBlendEquationEXT(cmd->buffer, 0, count, blend_equations.data());
}

void gpu_cmd_bind_shaders(gpu_commands* cmd, std::span<gpu_shader* const> shaders)
{
    auto* ctx = cmd->queue->ctx;

    u32 count = u32(shaders.size());

    std::vector<VkShaderStageFlagBits> stage_flags(count);
    std::vector<VkShaderEXT> shader_objects(count);

    for (u32 i = 0; i < shaders.size(); ++i) {
        stage_flags[i] = VkShaderStageFlagBits(shaders[i]->stage);
        shader_objects[i] = shaders[i]->shader;
    }

    ctx->vk.CmdBindShadersEXT(cmd->buffer, count, stage_flags.data(), shader_objects.data());
}

void gpu_cmd_reset_graphics_state(gpu_commands* cmd)
{
    auto* ctx = cmd->queue->ctx;

    ctx->vk.CmdSetAlphaToCoverageEnableEXT(cmd->buffer, false);
    ctx->vk.CmdSetSampleMaskEXT(           cmd->buffer, VK_SAMPLE_COUNT_1_BIT, ptr_to<VkSampleMask>(0xFFFF'FFFF));
    ctx->vk.CmdSetRasterizationSamplesEXT( cmd->buffer, VK_SAMPLE_COUNT_1_BIT);
    ctx->vk.CmdSetVertexInputEXT(          cmd->buffer, 0, nullptr, 0, nullptr);

    ctx->vk.CmdSetRasterizerDiscardEnable(cmd->buffer, false);
    ctx->vk.CmdSetPrimitiveRestartEnable( cmd->buffer, false);

    // Stencil tests

    ctx->vk.CmdSetStencilTestEnable(cmd->buffer, false);
    ctx->vk.CmdSetStencilOp(        cmd->buffer, VK_STENCIL_FACE_FRONT_AND_BACK,
        VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_NEVER);

    // Depth (extended)

    ctx->vk.CmdSetDepthBiasEnable(      cmd->buffer, false);
    ctx->vk.CmdSetDepthBoundsTestEnable(cmd->buffer, false);
    ctx->vk.CmdSetDepthBounds(          cmd->buffer, 0.f, 1.f);

    gpu_cmd_set_polygon_state(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL, 1.f);
    gpu_cmd_set_cull_state(   cmd, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    gpu_cmd_set_depth_stae(   cmd, false, false, VK_COMPARE_OP_ALWAYS);
}
