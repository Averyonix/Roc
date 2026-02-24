#pragma once

#include "gpu.hpp"

// -----------------------------------------------------------------------------

const char* wren_result_to_string(VkResult res);

VkResult wren_check(VkResult res, auto... allowed)
{
    if (res == VK_SUCCESS || (... || (res == allowed))) return res;

    log_error("VULKAN ERROR: {}, ({})", wren_result_to_string(res), int(res));

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

u32 wren_find_vk_memory_type_index(wren_context*, u32 type_filter, VkMemoryPropertyFlags properties);

VkFormatFeatureFlags wren_get_required_format_features(wren_format, flags<wren_image_usage>);

// -----------------------------------------------------------------------------

void wren_image_init(wren_image*);

// -----------------------------------------------------------------------------

void wren_init_descriptors(wren_context*);
void wren_allocate_image_descriptor(wren_image*);
void wren_allocate_sampler_descriptor(wren_sampler*);

// -----------------------------------------------------------------------------

ref<wren_queue> wren_queue_init(wren_context*, wren_queue_type, u32 family);

// -----------------------------------------------------------------------------

VkSemaphoreSubmitInfo wren_syncpoint_to_submit_info(const wren_syncpoint& syncpoint);
