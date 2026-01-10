#include "wren.hpp"

// -----------------------------------------------------------------------------

const char* wren_result_to_string(VkResult res);

VkResult wren_check(VkResult res, auto... allowed)
{
    if (res == VK_SUCCESS || (... || (res == allowed))) return res;

    log_error("VULKAN ERROR: {}, ({})\n{}", wren_result_to_string(res), int(res), wrei_stacktrace_dump(1));

    wrei_debugkill();
}

template<typename Container, typename Fn, typename... Args>
void wren_vk_enumerate(Container& container, Fn&& fn, Args&&... args)
{
    u32 count = static_cast<u32>(container.size());
    for (;;) {
        u32 old_count = count;
        if constexpr (std::same_as<VkResult, decltype(fn(args..., &count, nullptr))>) {
            wren_check(fn(args..., &count, container.data()), VK_INCOMPLETE);
        } else {
            fn(args..., &count, container.data());
        }

        container.resize(count);
        if (count <= old_count) return;
    }
}

inline
auto wren_vk_make_chain_in(std::span<void* const> structures)
{
    VkBaseInStructure* last = nullptr;
    for (auto* s : structures) {
        auto vk_base = static_cast<VkBaseInStructure*>(s);
        vk_base->pNext = last;
        last = vk_base;
    }

    return last;
};

// -----------------------------------------------------------------------------

void wren_register_formats(wren_context*);

// -----------------------------------------------------------------------------

u32 wren_find_vk_memory_type_index(wren_context*, u32 type_filter, VkMemoryPropertyFlags properties);

// -----------------------------------------------------------------------------

void wren_image_init(wren_image*);

// -----------------------------------------------------------------------------

void wren_init_descriptors(wren_context*);
void wren_allocate_image_descriptor(wren_image*);
void wren_allocate_sampler_descriptor(wren_sampler*);

// -----------------------------------------------------------------------------

ref<wren_queue> wren_queue_init(wren_context*, wren_queue_type, u32 family);

// -----------------------------------------------------------------------------

VkSemaphoreSubmitInfo              wren_syncpoint_to_submit_info(const wren_syncpoint& syncpoint);
std::vector<VkSemaphoreSubmitInfo> wren_syncpoints_to_submit_infos(std::span<const wren_syncpoint> syncpoints, const wren_syncpoint* extra = nullptr);

// -----------------------------------------------------------------------------

void wren_init_gbm_allocator(wren_context*);
void wren_destroy_gbm_allocator(wren_context*);

// -----------------------------------------------------------------------------

static constexpr VkFormatFeatureFlags wren_shm_texture_features
    = VK_FORMAT_FEATURE_TRANSFER_SRC_BIT
    | VK_FORMAT_FEATURE_TRANSFER_DST_BIT
    | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
    | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;

static constexpr VkImageUsageFlags wren_shm_texture_usage
    = VK_IMAGE_USAGE_SAMPLED_BIT
    | VK_IMAGE_USAGE_TRANSFER_DST_BIT
    | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

static constexpr VkFormatFeatureFlags wren_render_features
    = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT
    | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;

static constexpr VkImageUsageFlags wren_render_usage
    = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

static constexpr VkFormatFeatureFlags wren_dma_texture_features
    = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
    | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;

static constexpr VkImageUsageFlags wren_dma_texture_usage
    = VK_IMAGE_USAGE_SAMPLED_BIT
    | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

static constexpr VkFormatFeatureFlags wren_ycbcr_texture_features
    = VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT
    | VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT;
