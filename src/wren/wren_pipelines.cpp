#include "wren_internal.hpp"

ref<wren_pipeline> wren_pipeline_create(
    wren_context* ctx,
    wren_blend_mode blend_mode,
    wren_format format,
    std::span<const u32> spirv,
    const char* vertex_entry,
    const char* fragment_entry)
{
    ref pipeline = wrei_get_registry(ctx)->create<wren_pipeline>();
    pipeline->ctx = ctx;

    wren_check(ctx->vk.CreateGraphicsPipelines(ctx->device, nullptr, 1, wrei_ptr_to(VkGraphicsPipelineCreateInfo {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = wrei_ptr_to(VkPipelineRenderingCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &format->vk,
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
                .blendEnable = blend_mode != wren_blend_mode::none,
                .srcColorBlendFactor = blend_mode == wren_blend_mode::premultiplied
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
        .layout = ctx->pipeline_layout,
    }), nullptr, &pipeline->pipeline));

    return pipeline;
}

wren_pipeline::~wren_pipeline()
{
    ctx->vk.DestroyPipeline(ctx->device, pipeline, nullptr);
}
