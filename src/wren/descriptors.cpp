#include "internal.hpp"

void wren_init_descriptors(wren_context* ctx)
{
    auto& vk = ctx->vk;

    VkDescriptorBindingFlags binding_flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
        | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

    constexpr auto num_image_descriptors_each = 65536;
    constexpr auto num_sampler_descriptors    =    16;

    ctx->image_descriptor_allocator   = wren_descriptor_id_allocator(num_image_descriptors_each);
    ctx->sampler_descriptor_allocator = wren_descriptor_id_allocator(num_sampler_descriptors);

    wren_check(vk.CreateDescriptorSetLayout(ctx->device, wrei_ptr_to(VkDescriptorSetLayoutCreateInfo {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = wrei_ptr_to(VkDescriptorSetLayoutBindingFlagsCreateInfo {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT,
            .bindingCount = 2,
            .pBindingFlags = std::array { binding_flags, binding_flags }.data(),
        }),
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = 2,
        .pBindings = std::array {
            VkDescriptorSetLayoutBinding {
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                .descriptorCount = num_image_descriptors_each,
                .stageFlags = VK_SHADER_STAGE_ALL,
            },
            VkDescriptorSetLayoutBinding {
                .binding = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                .descriptorCount = num_sampler_descriptors,
                .stageFlags = VK_SHADER_STAGE_ALL,
            }
        }.data(),
    }), nullptr, &ctx->set_layout));

    wren_check(vk.CreatePipelineLayout(ctx->device, wrei_ptr_to(VkPipelineLayoutCreateInfo {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &ctx->set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = wrei_ptr_to(VkPushConstantRange {
            .stageFlags = VK_SHADER_STAGE_ALL,
            .size = 128,
        }),
    }), nullptr, &ctx->pipeline_layout));

    wren_check(vk.CreateDescriptorPool(ctx->device, wrei_ptr_to(VkDescriptorPoolCreateInfo {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = 1,
        .poolSizeCount = 2,
        .pPoolSizes = std::array {
            VkDescriptorPoolSize { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, num_image_descriptors_each },
            VkDescriptorPoolSize { VK_DESCRIPTOR_TYPE_SAMPLER, num_sampler_descriptors },
        }.data(),
    }), nullptr, &ctx->pool));

    wren_check(vk.AllocateDescriptorSets(ctx->device, wrei_ptr_to(VkDescriptorSetAllocateInfo {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = ctx->pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &ctx->set_layout,
    }), &ctx->set));
}

// -----------------------------------------------------------------------------

wren_descriptor_id_allocator::wren_descriptor_id_allocator(u32 count)
    : next_id(1)
    , capacity(count)
{}

std::optional<u32> wren_descriptor_id_allocator::allocate()
{
    if (!freelist.empty()) {
        auto id = freelist.back();
        freelist.pop_back();
        return id;
    }

    if (next_id >= capacity) return std::nullopt;

    return next_id++;
}

void wren_descriptor_id_allocator::free(u32 id)
{
    if (id) {
        freelist.emplace_back(id);
    }
}

// -----------------------------------------------------------------------------

void wren_allocate_image_descriptor(wren_image* image)
{
    auto* ctx = image->ctx.get();
    auto& vk = ctx->vk;

    auto id = ctx->image_descriptor_allocator.allocate();
    if (!id) {
        log_error("No more available image descriptors ids");
        return;
    }

    image->id = *id;

    log_warn("Image allocated ID: {}", image->id);

    vk.UpdateDescriptorSets(ctx->device, 1, std::array {
        VkWriteDescriptorSet {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = ctx->set,
            .dstBinding = 0,
            .dstArrayElement = image->id,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo = wrei_ptr_to(VkDescriptorImageInfo {
                .imageView = image->view,
                .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            }),
        },
    }.data(), 0, nullptr);
}

// -----------------------------------------------------------------------------

void wren_allocate_sampler_descriptor(wren_sampler* sampler)
{
    auto* ctx = sampler->ctx.get();
    auto& vk = ctx->vk;

    auto id = ctx->sampler_descriptor_allocator.allocate();
    if (!id) {
        log_error("No more available image descriptors ids");
        return;
    }

    sampler->id = *id;

    log_warn("Sampler allocated ID: {}", sampler->id);

    vk.UpdateDescriptorSets(ctx->device, 1, std::array {
        VkWriteDescriptorSet {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = ctx->set,
            .dstBinding = 1,
            .dstArrayElement = sampler->id,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
            .pImageInfo = wrei_ptr_to(VkDescriptorImageInfo {
                .sampler = sampler->sampler,
            }),
        },
    }.data(), 0, nullptr);
}
