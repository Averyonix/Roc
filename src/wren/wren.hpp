#pragma once

#include "wrei/ref.hpp"
#include "wrei/types.hpp"

#include "wren_functions.hpp"

// -----------------------------------------------------------------------------

struct wren_descriptor_id_allocator
{
    std::vector<u32> freelist;
    u32 next_id;
    u32 capacity;

    wren_descriptor_id_allocator() = default;
    wren_descriptor_id_allocator(u32 count);

    std::optional<u32> allocate();
    void free(u32);
};

// -----------------------------------------------------------------------------

struct wren_context : wrei_object
{
    struct {
        WREN_DECLARE_FUNCTION(GetInstanceProcAddr)
        WREN_DECLARE_FUNCTION(CreateInstance)
        WREN_INSTANCE_FUNCTIONS(WREN_DECLARE_FUNCTION)
        WREN_DEVICE_FUNCTIONS(  WREN_DECLARE_FUNCTION)
    } vk;

    void* vulkan1;

    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;

    vkwsi_context* vkwsi;

    VmaAllocator vma;

    u32 queue_family;
    VkQueue queue;

    VkCommandPool cmd_pool;
    VkCommandBuffer cmd;

    VkDescriptorSetLayout set_layout;
    VkPipelineLayout pipeline_layout;
    VkDescriptorPool pool;
    VkDescriptorSet set;

    wren_descriptor_id_allocator image_descriptor_allocator;
    wren_descriptor_id_allocator sampler_descriptor_allocator;

    ~wren_context();
};

enum class wren_features
{
    none,
    dmabuf = 1 << 0,
};
WREI_DECORATE_FLAG_ENUM(wren_features)

ref<wren_context> wren_create(wrei_registry*, wren_features);

VkCommandBuffer wren_begin_commands( wren_context*);
void            wren_submit_commands(wren_context*, VkCommandBuffer);

// -----------------------------------------------------------------------------

void wren_wait_for_timeline_value(wren_context*, const VkSemaphoreSubmitInfo&);

// -----------------------------------------------------------------------------

struct wren_buffer : wrei_object
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

ref<wren_buffer> wren_buffer_create(wren_context*, usz size);

// -----------------------------------------------------------------------------

struct wren_image : wrei_object
{
    wren_context* ctx;

    VkImage image;
    VkImageView view;
    VkDeviceMemory memory;
    VmaAllocation vma_allocation;
    VkExtent3D extent;

    u32 id;

    ~wren_image();
};

ref<wren_image> wren_image_create(wren_context*, VkExtent2D extent, VkFormat format);
void wren_image_update(wren_image*, const void* data);

void wren_transition(wren_context* vk, VkCommandBuffer cmd, VkImage image,
        VkPipelineStageFlags2 src, VkPipelineStageFlags2 dst,
        VkAccessFlags2 src_access, VkAccessFlags2 dst_access,
        VkImageLayout old_layout, VkImageLayout new_layout);

// -----------------------------------------------------------------------------

struct wren_sampler : wrei_object
{
    wren_context* ctx;

    VkSampler sampler;

    u32 id;

    ~wren_sampler();
};

ref<wren_sampler> wren_sampler_create(wren_context*);

// -----------------------------------------------------------------------------

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

ref<wren_image> wren_image_import_dmabuf(wren_context*, const wren_dma_params& params);
