#pragma once

#include "wrei/types.hpp"
#include "wrei/log.hpp"
#include "wrei/ref.hpp"

struct wren_context;

const char* wren_result_to_string(VkResult res);

VkResult wren_check(VkResult res, auto... allowed)
{
    if (res == VK_SUCCESS || (... || (res == allowed))) return res;

    log_error("VULKAN ERROR: {}, ({})", wren_result_to_string(res), int(res));
    std::cout << std::stacktrace::current();

    return res;
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

void wren_wait_for_timeline_value(wren_context*, const VkSemaphoreSubmitInfo&);

struct wren_buffer : wrei_ref_counted
{
    wren_context* ctx;

    VkBuffer buffer;
    VmaAllocation vma_allocation;
    VkDeviceAddress device_address;
    void* host_address;

    template<typename T>
    T* device() const { return reinterpret_cast<T*>(device_address); }

    template<typename T>
    T* host() const { return reinterpret_cast<T*>(host_address); }

    ~wren_buffer();
};

wrei_ref<wren_buffer> wren_buffer_create(wren_context*, usz size);

u32 wren_find_vk_memory_type_index(wren_context* vk, u32 type_filter, VkMemoryPropertyFlags properties);

struct wren_image : wrei_ref_counted
{
    wren_context* ctx;

    VkImage image;
    VkImageView view;
    VkDeviceMemory memory;
    VmaAllocation vma_allocation;
    VkExtent3D extent;

    ~wren_image();
};

wrei_ref<wren_image> wren_image_create(wren_context*, VkExtent2D extent, VkFormat format);
void wren_image_update(wren_image*, const void* data);

VkSampler wren_sampler_create(wren_context*);
void wren_sampler_destroy(wren_context*, VkSampler);

void wren_transition(wren_context* vk, VkCommandBuffer cmd, VkImage image,
        VkPipelineStageFlags2 src, VkPipelineStageFlags2 dst,
        VkAccessFlags2 src_access, VkAccessFlags2 dst_access,
        VkImageLayout old_layout, VkImageLayout new_layout);

struct wren_format
{
    u32 drm;
    VkFormat vk;
    VkFormat vk_srgb;
	bool is_ycbcr;
};

std::span<const wren_format> wren_get_formats();
std::optional<wren_format> wren_find_format_from_vulkan(VkFormat);
std::optional<wren_format> wren_find_format_from_drm(u32 drm_format);
void wren_enumerate_drm_modifiers(wren_context*, const wren_format&, std::vector<VkDrmFormatModifierProperties2EXT>&);

// -----------------------------------------------------------------------------

constexpr static u32 wren_dma_max_planes = 4;

struct wren_dma_plane
{
    int fd;
    u32 plane_idx;
    u32 offset;
    u32 stride;
    u64 drm_modifier;
};

struct wren_dma_params
{
    std::vector<wren_dma_plane> planes;
    VkExtent2D extent;
    wren_format format;
    zwp_linux_buffer_params_v1_flags flags;
};

wrei_ref<wren_image> wren_image_import_dmabuf(wren_context*, const wren_dma_params& params);
