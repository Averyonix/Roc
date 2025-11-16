#pragma once

#include "protocol.hpp"
#include "util.hpp"

#include "wrei/pch.hpp"
#include "wrei/types.hpp"
#include "wrei/util.hpp"
#include "wrei/log.hpp"

#include "wren/wren.hpp"

// -----------------------------------------------------------------------------

struct wroc_server;

void wroc_run(int argc, char* argv[]);
void wroc_terminate(wroc_server*);

// -----------------------------------------------------------------------------

struct wren_renderer;

// -----------------------------------------------------------------------------

struct wroc_backend;

void wroc_backend_init(wroc_server*);
void wroc_backend_destroy(wroc_backend*);

// -----------------------------------------------------------------------------

struct wroc_output : wrei_object
{
    wroc_server* server;

    vec2i32 size;

    VkSurfaceKHR vk_surface;
    VkSemaphore timeline;
    u64 timeline_value = 0;
    VkSurfaceFormatKHR format;
    vkwsi_swapchain* swapchain;

    vec2i32 position;
};

vkwsi_swapchain_image wroc_output_acquire_image(wroc_output*);

void wroc_backend_output_create(wroc_backend*);
void wroc_backend_output_destroy(wroc_output*);

// -----------------------------------------------------------------------------

struct wroc_xdg_wm_base : wrei_object
{
    wroc_server* server;

    wrei_wl_resource xdg_wm_base;
};

struct wroc_wl_compositor : wrei_object
{
    wroc_server* server;

    wrei_wl_resource wl_compositor;
};

struct wroc_wl_region : wrei_object
{
    wroc_server* server;

    wrei_wl_resource wl_region;

    wrei_region region;
};

struct wroc_wl_buffer;

// -----------------------------------------------------------------------------

struct wroc_surface_addon : wrei_object
{
    virtual void on_initial_commit() = 0;
    virtual void on_commit() = 0;
    virtual void on_ack_configure(u32 serial) {}
};

enum class wroc_surface_committed_state : u32
{
    none,
    buffer       = 1 << 0,
    offset       = 1 << 1,
    input_region = 1 << 2,
    buffer_scale = 1 << 3,
};
WREI_DECORATE_FLAG_ENUM(wroc_surface_committed_state)

struct wroc_surface_state
{
    wroc_surface_committed_state committed;

    wrei_ref<wroc_wl_buffer> buffer;
    wrei_wl_resource_list frame_callbacks;
    vec2i32 offset;
    wrei_region input_region;
    double buffer_scale;
};

struct wroc_surface : wrei_object
{
    wroc_server* server;

    wrei_wl_resource wl_surface;

    bool initial_commit = true;

    wroc_surface_state pending;
    wroc_surface_state current = {
        .input_region = wrei_region({{0, 0}, {INT32_MAX, INT32_MAX}}),
        .buffer_scale = 1.f
    };

    wroc_surface_addon* role_addon;

    ~wroc_surface();
};

bool wroc_surface_point_accepts_input(wroc_surface*, vec2f64 point);

// -----------------------------------------------------------------------------

enum class wroc_xdg_surface_committed_state : u32
{
    none,
    geometry = 1 << 0,
};
WREI_DECORATE_FLAG_ENUM(wroc_xdg_surface_committed_state)

struct wrox_xdg_surface_state
{
    wroc_xdg_surface_committed_state committed = wroc_xdg_surface_committed_state::none;

    rect2i32 geometry;
};

struct wroc_xdg_surface : wroc_surface_addon
{
    wrei_ref<wroc_surface> surface;

    wrei_wl_resource xdg_surface;

    wroc_surface_addon* xdg_role_addon;

    wrox_xdg_surface_state pending;
    wrox_xdg_surface_state current;

    vec2i32 position;

    u32 sent_configure_serial = {};
    u32 acked_configure_serial = {};

    virtual void on_initial_commit() final override;
    virtual void on_commit() final override;
    ~wroc_xdg_surface();

    static
    wroc_xdg_surface* try_from(wroc_surface* surface)
    {
        return surface ? dynamic_cast<wroc_xdg_surface*>(surface->role_addon) : nullptr;
    }
};

rect2i32 wroc_xdg_surface_get_geometry(wroc_xdg_surface* surface);
void wroc_xdg_surface_flush_configure(wroc_xdg_surface* surface);

// -----------------------------------------------------------------------------

enum class wroc_xdg_toplevel_configure_state : u32
{
    none,
    bounds = 1 << 0,
    size   = 1 << 1,
    states = 1 << 2,
};
WREI_DECORATE_FLAG_ENUM(wroc_xdg_toplevel_configure_state)

enum class wroc_xdg_toplevel_committed_state : u32
{
    none,
    title  = 1 << 0,
    app_id = 1 << 1
};
WREI_DECORATE_FLAG_ENUM(wroc_xdg_toplevel_committed_state)

struct wroc_xdg_toplevel_state
{
    wroc_xdg_toplevel_committed_state committed = wroc_xdg_toplevel_committed_state::none;

    std::string title;
    std::string app_id;
};

struct wroc_xdg_toplevel : wroc_surface_addon
{
    wrei_ref<wroc_xdg_surface> base;

    wrei_wl_resource xdg_toplevel;

    wroc_xdg_toplevel_state pending;
    wroc_xdg_toplevel_state current;

    vec2i32 bounds;
    vec2i32 size;
    std::vector<xdg_toplevel_state> states;
    wroc_xdg_toplevel_configure_state pending_configure = {};

    virtual void on_initial_commit() final override;
    virtual void on_commit() final override;
    virtual void on_ack_configure(u32 serial) final override;

    ~wroc_xdg_toplevel();

    static
    wroc_xdg_toplevel* try_from(wroc_xdg_surface* xdg_surface)
    {
        return xdg_surface ? dynamic_cast<wroc_xdg_toplevel*>(xdg_surface->xdg_role_addon) : nullptr;
    }

    static
    wroc_xdg_toplevel* try_from(wroc_surface* surface)
    {
        return try_from(wroc_xdg_surface::try_from(surface));
    }
};

void wroc_xdg_toplevel_set_bounds(wroc_xdg_toplevel*, vec2i32 bounds);
void wroc_xdg_toplevel_set_size(wroc_xdg_toplevel*, vec2i32 size);
void wroc_xdg_toplevel_set_state(wroc_xdg_toplevel*, xdg_toplevel_state, bool enabled);
void wroc_xdg_toplevel_flush_configure(wroc_xdg_toplevel*);

// -----------------------------------------------------------------------------

enum class wroc_wl_buffer_type : u32
{
    shm,
    dma,
};

struct wroc_wl_buffer : wrei_object
{
    wroc_server* server;

    wroc_wl_buffer_type type;

    wrei_wl_resource wl_buffer;

    vec2i32 extent;

    wrei_ref<wren_image> image;

    bool locked = false;

    void lock();
    void unlock();

    virtual void on_commit() = 0;
};

// -----------------------------------------------------------------------------

struct wroc_wl_shm : wrei_object
{
    wroc_server* server;

    wrei_wl_resource wl_shm;
};

struct wroc_wl_shm_pool : wrei_object
{
    wroc_server* server;

    wrei_wl_resource wl_shm_pool;

    i32 size;
    int fd;
    void* data;

    ~wroc_wl_shm_pool();
};

struct wroc_shm_buffer : wroc_wl_buffer
{
    wrei_ref<wroc_wl_shm_pool> pool;

    i32 offset;
    i32 stride;
    wl_shm_format format;

    virtual void on_commit() final override;
};

// -----------------------------------------------------------------------------

struct wroc_zwp_linux_buffer_params : wrei_object
{
    wroc_server* server;

    wrei_wl_resource zwp_linux_buffer_params_v1;

    wren_dma_params params;

    ~wroc_zwp_linux_buffer_params();
};

struct wroc_dma_buffer : wroc_wl_buffer
{
    virtual void on_commit() final override;
};

// -----------------------------------------------------------------------------

struct wroc_seat : wrei_object
{
    wroc_server* server;

    struct wroc_keyboard* keyboard;
    struct wroc_pointer*  pointer;

    std::string name;

    wrei_wl_resource_list wl_seat;
};

// -----------------------------------------------------------------------------

enum class wroc_modifiers : u32
{
    mod    = 1 << 0,
    super  = 1 << 1,
    shift  = 1 << 2,
    ctrl   = 1 << 3,
    alt    = 1 << 4,
    num    = 1 << 5,
};
WREI_DECORATE_FLAG_ENUM(wroc_modifiers)

static constexpr std::pair<wroc_modifiers, const char*> wroc_modifier_xkb_names[] = {
    { wroc_modifiers::super, XKB_MOD_NAME_LOGO },
    { wroc_modifiers::shift, XKB_MOD_NAME_SHIFT },
    { wroc_modifiers::ctrl,  XKB_MOD_NAME_CTRL },
    { wroc_modifiers::alt,   XKB_MOD_NAME_ALT },
    { wroc_modifiers::num,   XKB_MOD_NAME_NUM },
};

struct wroc_keyboard : wrei_object
{
    wroc_server* server;

    wrei_wl_resource_list wl_keyboards;
    wrei_wl_resource focused;
    wrei_weak<wroc_surface> focused_surface;

    struct xkb_context* xkb_context;
    struct xkb_state*   xkb_state;
    struct xkb_keymap*  xkb_keymap;

    std::array<std::pair<wroc_modifiers, xkb_mod_mask_t>, std::size(wroc_modifier_xkb_names)> xkb_mod_masks;
    wroc_modifiers active_modifiers;

    int keymap_fd = -1;
    i32 keymap_size;

    std::vector<u32> pressed = {};

    i32 rate;
    i32 delay;

    ~wroc_keyboard();
};

wroc_modifiers wroc_keyboard_get_active_modifiers(wroc_keyboard*);

void wroc_keyboard_clear_focus(wroc_keyboard*);
void wroc_keyboard_enter(wroc_keyboard*, wroc_surface*);

// -----------------------------------------------------------------------------

struct wroc_pointer : wrei_object
{
    wroc_server* server;

    wrei_wl_resource_list wl_pointers;
    wrei_wl_resource focused;
    wrei_weak<wroc_surface> focused_surface;

    vec2f64 layout_position;
};

// -----------------------------------------------------------------------------

struct wroc_renderer : wrei_object
{
    wroc_server* server;

    wrei_ref<wren_context> wren;

    VkFormat output_format = VK_FORMAT_B8G8R8A8_UNORM;

    VkPipeline pipeline;

    wrei_ref<wren_image> image;
    wrei_ref<wren_sampler> sampler;

    ~wroc_renderer();
};

void wroc_renderer_create(wroc_server*);
void wroc_render_frame(wroc_output* output);

enum class wroc_interaction_mode : u32
{
    normal,
    move,
    size,
};

enum class wroc_edges : u32
{
    left,
    top,
    right,
    bottom,
};
WREI_DECORATE_FLAG_ENUM(wroc_edges)

struct wroc_server : wrei_object
{
    wroc_backend*  backend;
    wrei_ref<wroc_renderer> renderer;
    wrei_ref<wroc_seat>     seat;

    wroc_modifiers main_mod = wroc_modifiers::alt;

    std::chrono::steady_clock::time_point epoch;

    wl_display* display;
    wl_event_loop* event_loop;

    std::vector<wroc_surface*> surfaces;
    wrei_weak<wroc_xdg_toplevel> toplevel_under_cursor;

    wroc_interaction_mode interaction_mode;

    struct {
        wrei_weak<wroc_xdg_toplevel> grabbed_toplevel;
        vec2f64 pointer_grab;
        vec2i32 surface_grab;
        wroc_edges edges;
    } movesize;
};

u32 wroc_get_elapsed_milliseconds(wroc_server*);
wroc_modifiers wroc_get_active_modifiers(wroc_server*);
