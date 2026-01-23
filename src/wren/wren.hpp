#pragma once

#include "wrei/object.hpp"
#include "wrei/types.hpp"
#include "wrei/event.hpp"

#include "functions.hpp"

// Import DRM sync objects as Vulkan timeline semaphores.
// WARNING: This is not valid behaviour by the Vulkan spec and only works
// when the driver uses syncobjs as the underlying type for its opaque fds.
#define WREN_IMPORT_SYNCOBJ_AS_TIMELINE 0

// -----------------------------------------------------------------------------

struct wren_image;
struct wren_semaphore;
struct wren_commands;
struct wren_queue;

// -----------------------------------------------------------------------------

enum class wren_descriptor_id : u32 { invalid = 0 };

struct wren_descriptor_id_allocator
{
    std::vector<wren_descriptor_id> freelist;
    u32 next_id;
    u32 capacity;

    wren_descriptor_id_allocator() = default;
    wren_descriptor_id_allocator(u32 count);

    std::optional<wren_descriptor_id> allocate();
    void free(wren_descriptor_id);
};

// -----------------------------------------------------------------------------

using wren_drm_format   = u32;
using wren_drm_modifier = u64;

struct wren_format_t
{
    const char* name;
    wren_drm_format drm;
    VkFormat vk;
    VkFormat vk_srgb;
    wl_shm_format shm;
    bool is_ycbcr;
    bool has_alpha;

    // NOTE: These are singleton objects per supported format
    //       so we delete copy/move and protect construction

    constexpr wren_format_t(const struct wren_format_t_create_params&);
    WREI_DELETE_COPY_MOVE(wren_format_t)
};

extern const wren_format_t wren_formats[];

struct wren_format
{
    i32 _index;

    constexpr wren_format(                      ) : _index(-1              ) {}
    constexpr wren_format(const wren_format_t* f) : _index(f - wren_formats) {}

    constexpr const wren_format_t* operator->() const noexcept { return &wren_formats[_index]; };
    constexpr                   operator bool() const noexcept { return _index >= 0;           }

    constexpr bool operator==(const wren_format&) const noexcept = default;
};

template<>
struct std::hash<wren_format>
{
    usz operator()(const wren_format& f) const noexcept
    {
        return std::hash<i32>()(f._index);
    }
};

struct wren_format_modifier_props
{
    VkDrmFormatModifierProperties2EXT props;
    vec2u32 max_extent;
    bool has_mutable_srgb;
};

struct wren_format_props
{
    wren_format format;
    struct {
        vec2u32 max_extent;
        VkFormatFeatureFlags features;
        bool has_mutable_srgb;
    } shm;
    struct {
        std::vector<wren_format_modifier_props> render_mods;
        std::vector<wren_format_modifier_props> texture_mods;
    } dmabuf;
};

wren_format wren_format_from_drm(wren_drm_format);
wren_format wren_format_from_shm(wl_shm_format);

using wren_format_modifier_set = ankerl::unordered_dense::set<wren_drm_modifier>;

struct wren_format_set
{
    ankerl::unordered_dense::map<wren_format, wren_format_modifier_set> entries;

    void add(wren_format format, wren_drm_modifier modifier)
    {
        entries[format].insert(modifier);
    }

    usz   size() const { return entries.size(); }
    bool empty() const { return !entries.empty(); }

    auto begin() const { return entries.begin(); }
    auto   end() const { return entries.end(); }
};

std::vector<wren_drm_modifier> wren_intersect_format_modifiers(std::span<const wren_format_modifier_set* const> sets);

const wren_format_props* wren_get_format_props(wren_context*, wren_format);

std::string wren_drm_modifier_get_name(wren_drm_modifier);

// -----------------------------------------------------------------------------

enum class wren_features : u32
{
    dmabuf = 1 << 0,
};
WREI_DECORATE_FLAG_ENUM(wren_features)

struct wren_context : wrei_object
{
    wren_features features;

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

    dev_t dev_id;
    int drm_fd;

    VmaAllocator vma;

    struct {
        u32 active_images;
        usz active_image_memory;

        u32 active_buffers;
        usz active_buffer_memory;

        u32 active_samplers;
    } stats;

    ref<wren_queue> graphics_queue;
    ref<wren_queue> transfer_queue;
    ref<wrei_event_loop> event_loop;

    std::vector<VkSemaphore> free_binary_semaphores;

    VkDescriptorSetLayout set_layout;
    VkPipelineLayout pipeline_layout;
    VkDescriptorPool pool;
    VkDescriptorSet set;

    wren_descriptor_id_allocator image_descriptor_allocator;
    wren_descriptor_id_allocator sampler_descriptor_allocator;

    wren_format_set dmabuf_texture_formats;
    wren_format_set dmabuf_render_formats;
    wren_format_set shm_texture_formats;

    ankerl::unordered_dense::map<wren_format, wren_format_props> format_props;

    gbm_device* gbm;

    ~wren_context();
};

ref<wren_context> wren_create(wren_features, wrei_event_loop*);

// -----------------------------------------------------------------------------

enum class wren_queue_type : u32
{
    graphics,
    transfer,
};

struct wren_queue : wrei_object
{
    wren_context* ctx;

    wren_queue_type type;
    u32 family;
    VkQueue queue;

    VkCommandPool cmd_pool;
    VkCommandBuffer cmd;

    ref<wren_semaphore> queue_sema;
    std::deque<ref<wren_commands>> submissions;

    std::atomic<u64> wait_thread_submitted;
    std::jthread     wait_thread;

    ~wren_queue();
};

wren_queue* wren_get_queue(wren_context*, wren_queue_type);

// -----------------------------------------------------------------------------

enum class wren_semaphore_type : u32
{
    binary,
    timeline,
    syncobj,
};

struct wren_semaphore : wrei_object
{
    wren_context* ctx;

    wren_semaphore_type type;
    union {
        VkSemaphore semaphore;
        u32 syncobj;
    };

    ~wren_semaphore();
};

struct wren_syncpoint
{
    wren_semaphore* semaphore;
    u64 value = 0;
    VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
};

ref<wren_semaphore> wren_semaphore_create(wren_context*, wren_semaphore_type, u64 initial_value = 0);
ref<wren_semaphore> wren_semaphore_import_syncobj(wren_context*, int syncobj_fd);
ref<wren_semaphore> wren_semaphore_import_syncfile(wren_context*, int sync_fd);
int                 wren_semaphore_export_syncfile(wren_semaphore*);

u64  wren_semaphore_get_value(   wren_semaphore*);
void wren_semaphore_wait_value(  wren_semaphore*, u64 value);
void wren_semaphore_signal_value(wren_semaphore*, u64 value);

// -----------------------------------------------------------------------------

struct wren_commands : wrei_object
{
    wren_queue* queue;

    VkCommandBuffer buffer;
    std::vector<ref<wrei_object>> objects;

    u64 submitted_value;

    ~wren_commands();
};

ref<wren_commands> wren_commands_begin(wren_queue*);

void wren_commands_protect_object(wren_commands*, wrei_object*);
void wren_commands_submit(       wren_commands*, std::span<const wren_syncpoint> waits, std::span<const wren_syncpoint> signals);

// TODO: This is a blocking operation and a temporary solution
//       Replace with an asynchronous callback
void wren_wait_idle(wren_context*);
void wren_wait_idle(wren_queue*);

// -----------------------------------------------------------------------------

struct wren_buffer : wrei_object
{
    wren_context* ctx;

    VkBuffer buffer;
    VmaAllocation vma_allocation;
    VkDeviceAddress device_address;
    void* host_address;
    usz size;

    template<typename T>
    T* device(usz byte_offset = 0) const
    {
        return reinterpret_cast<T*>(device_address + byte_offset);
    }

    template<typename T>
    T* host(usz byte_offset = 0) const
    {
        return reinterpret_cast<T*>(reinterpret_cast<byte*>(host_address) + byte_offset);
    }

    ~wren_buffer();
};

ref<wren_buffer> wren_buffer_create(wren_context*, usz size);

// -----------------------------------------------------------------------------

template<typename T>
struct wren_array_element_proxy
{
    T* host_value;

    void operator=(const T& value)
    {
        std::memcpy(host_value, &value, sizeof(value));
    }
};

template<typename T>
struct wren_array
{
    ref<wren_buffer> buffer;
    usz count = 0;
    usz byte_offset = 0;

    wren_array() = default;

    wren_array(const auto& buffer, usz count, usz byte_offset = {})
        : buffer(buffer)
        , count(count)
        , byte_offset(byte_offset)
    {}

    T* device() const
    {
        return buffer->device<T>(byte_offset);
    }

    T* host() const
    {
        return buffer->host<T>(byte_offset);
    }

    wren_array_element_proxy<T> operator[](usz index) const
    {
        return {buffer->host<T>(byte_offset) + index};
    }
};

// -----------------------------------------------------------------------------

enum class wren_image_usage : u32
{
    transfer = 1 << 0,  // Transfer dst
    texture  = 1 << 1,  // Sampled
    render   = 1 << 2,  // Color attachment
    scanout  = 1 << 3,  // Suitable for direct scanout, must match swapchain format
    cursor   = 1 << 4,  // Suitable for hardware cursor overlay
};
WREI_DECORATE_FLAG_ENUM(wren_image_usage)

VkImageUsageFlags wren_image_usage_to_vk(wren_image_usage);

struct wren_image : wrei_object
{
    wren_context* ctx;

    wren_format format;

    VkImage image;
    VkImageView view;
    vec2u32 extent;

    wren_descriptor_id id;

    wren_image_usage usage;

    virtual ~wren_image() = 0;
};

ref<wren_image> wren_image_create(wren_context*, vec2u32 extent, wren_format, wren_image_usage);
void wren_image_update(wren_commands*, wren_image*, const void* data);
void wren_image_update_immed(wren_image*, const void* data);

void wren_transition(wren_context* vk, wren_commands*, wren_image*,
        VkPipelineStageFlags2 src, VkPipelineStageFlags2 dst,
        VkAccessFlags2 src_access, VkAccessFlags2 dst_access,
        VkImageLayout old_layout, VkImageLayout new_layout);

// -----------------------------------------------------------------------------

struct wren_sampler : wrei_object
{
    wren_context* ctx;

    VkSampler sampler;

    wren_descriptor_id id;

    ~wren_sampler();
};

ref<wren_sampler> wren_sampler_create(wren_context*, VkFilter mag, VkFilter min);

// -----------------------------------------------------------------------------

enum class wren_blend_mode : u32
{
    none,
    premultiplied,
    postmultiplied,
};

struct wren_pipeline : wrei_object
{
    wren_context* ctx;

    VkPipeline pipeline;

    ~wren_pipeline();
};

ref<wren_pipeline> wren_pipeline_create(wren_context*,
    wren_blend_mode,
    wren_format,
    std::span<const u32> spirv,
    const char* vertex_entry,
    const char* fragment_entry);

// -----------------------------------------------------------------------------

constexpr static u32 wren_dma_max_planes = 4;

struct wren_dma_plane
{
    int fd;
    u32 offset;
    u32 stride;
};

struct wren_dma_params
{
    struct {
        wren_dma_plane data[wren_dma_max_planes];
        u32 count;

        auto begin(this auto&& self) { return self.data; }
        auto   end(this auto&& self) { return self.data + self.count; }

        auto& operator[](this auto&& self, usz i) { return self.data[i]; }
    } planes;

    vec2u32 extent;
    wren_format format;
    wren_drm_modifier modifier;

    zwp_linux_buffer_params_v1_flags flags;
};

struct wren_image_dmabuf : wren_image
{
    wren_dma_params dma_params;

    std::array<VkDeviceMemory, wren_dma_max_planes> memory_planes;

    struct {
        usz allocation_size;
    } stats;

    ~wren_image_dmabuf();
};

ref<wren_image_dmabuf> wren_image_import_dmabuf(wren_context*, const wren_dma_params& params, wren_image_usage);

// -----------------------------------------------------------------------------

template<typename T>
struct wren_image_handle
{
    u32 image   : 20 = {};
    u32 sampler : 12 = {};

    wren_image_handle() = default;

    wren_image_handle(wren_image* image, wren_sampler* sampler)
        : image(std::to_underlying(image->id))
        , sampler(std::to_underlying(sampler->id))
    {}
};


// -----------------------------------------------------------------------------

struct wren_image_swapchain : wren_image
{
    ~wren_image_swapchain();
};

struct wren_swapchain : wrei_object
{
    wren_context* ctx;

    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;

    wren_format format;
    VkColorSpaceKHR color_space;

    vec2u32 extent;

    struct {
        vec2u32 extent;
    } pending;

    std::atomic<bool> can_acquire;
    std::atomic<bool> destroy_requested;

    bool acquire_ready = false;

    ref<wren_semaphore> acquire_semaphore;
    std::jthread        acquire_thread;
    std::move_only_function<void()> acquire_callback;

    static constexpr u32 invalid_index = ~0u;
    u32 current_index = invalid_index;
    std::vector<ref<wren_image_swapchain>> images;

    struct resources
    {
        std::vector<ref<wrei_object>> objects;
    };
    std::vector<resources> resources;

    ~wren_swapchain();
};

ref<wren_swapchain> wren_swapchain_create(wren_context*, VkSurfaceKHR, wren_format);

void                                   wren_swapchain_resize(       wren_swapchain*, vec2u32 extent);
std::pair<wren_image*, wren_syncpoint> wren_swapchain_acquire_image(wren_swapchain*);
void                                   wren_swapchain_present(      wren_swapchain*, std::span<const wren_syncpoint> waits);
