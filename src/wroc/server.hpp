#pragma once

#include "protocol.hpp"

#include "wrei/pch.hpp"
#include "wrei/types.hpp"
#include "wrei/util.hpp"
#include "wrei/log.hpp"

#include "wren/wren_helpers.hpp"

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

struct wroc_output
{
    wroc_server* server;

    wrei_vec2i32 size;

    VkSurfaceKHR vk_surface;
    VkSemaphore timeline;
    u64 timeline_value = 0;
    VkSurfaceFormatKHR format;
    vkwsi_swapchain* swapchain;

    wrei_vec2f64 position;
};

void wroc_output_added(wroc_output*);
void wroc_output_removed(wroc_output*);
void wroc_output_frame(wroc_output*);

vkwsi_swapchain_image wroc_output_acquire_image(wroc_output*);

void wroc_backend_output_create(wroc_backend*);
void wroc_backend_output_destroy(wroc_output*);

// -----------------------------------------------------------------------------

struct wroc_xdg_wm_base : wrei_ref_counted
{
    wroc_server* server;

    wl_resource* xdg_wm_base;
};

struct wroc_wl_compositor : wrei_ref_counted
{
    wroc_server* server;

    wl_resource* wl_compositor;
};

struct wroc_wl_region : wrei_ref_counted
{
    wroc_server* server;

    wl_resource* wl_region;

    wrei_region region;
};

struct wroc_wl_buffer;

struct wroc_surface_addon : wrei_ref_counted
{
    virtual void on_initial_commit() = 0;
    virtual void on_commit() = 0;
};

struct wroc_surface : wrei_ref_counted
{
    wroc_server* server;

    wl_resource* wl_surface;

    bool initial_commit = true;

    struct {
        bool buffer_was_set;
        wrei_ref<wroc_wl_buffer> buffer;
        std::vector<wl_resource*> frame_callbacks;
        std::optional<wrei_vec2i32> offset;
        std::optional<wrei_region> input_region;
        std::optional<double> buffer_scale; // TODO
    } pending;

    struct {
        wrei_ref<wroc_wl_buffer> buffer;
        std::vector<wl_resource*> frame_callbacks;
        wrei_vec2i32 offset = {};
        wrei_region input_region = wrei_region({{0, 0}, {INT32_MAX, INT32_MAX}});
        double buffer_scale = 1.f;
    } current;

    wroc_surface_addon* role_addon;

    ~wroc_surface();
};

bool wroc_surface_point_accepts_input(wroc_surface*, wrei_vec2f64 point);

// -----------------------------------------------------------------------------

struct wroc_xdg_surface : wroc_surface_addon
{
    wrei_ref<wroc_surface> surface;

    wl_resource* xdg_surface;

    wroc_surface_addon* xdg_role_addon;

    struct state
    {
        std::optional<wrei_rect<i32>> geometry;
    };

    state pending;
    state current;

    wrei_vec2f64 position;

    virtual void on_initial_commit() final override;
    virtual void on_commit() final override;
    ~wroc_xdg_surface();

    static
    wroc_xdg_surface* try_from(wroc_surface* surface)
    {
        return surface ? dynamic_cast<wroc_xdg_surface*>(surface->role_addon) : nullptr;
    }
};

wrei_rect<i32> wroc_xdg_surface_get_geometry(wroc_xdg_surface* surface);

enum class wroc_xdg_toplevel_configure_state
{
    none,
    bounds = 1 << 0,
    size   = 1 << 1,
    states = 1 << 2,
};
WREI_DECORATE_FLAG_ENUM(wroc_xdg_toplevel_configure_state)

struct wroc_xdg_toplevel : wroc_surface_addon
{
    wrei_ref<wroc_xdg_surface> base;

    wl_resource* xdg_toplevel;

    struct {
        std::optional<std::string> title;
        std::optional<std::string> app_id;
    } pending;

    struct {
        std::string title;
        std::string app_id;
    } current;

    wrei_vec2i32 bounds;
    wrei_vec2i32 size;
    std::vector<xdg_toplevel_state> states;
    wroc_xdg_toplevel_configure_state pending_configure = {};

    virtual void on_initial_commit() final override;
    virtual void on_commit() final override;
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

void wroc_xdg_toplevel_set_bounds(wroc_xdg_toplevel*, wrei_vec2i32 bounds);
void wroc_xdg_toplevel_set_size(wroc_xdg_toplevel*, wrei_vec2i32 size);
void wroc_xdg_toplevel_set_state(wroc_xdg_toplevel*, xdg_toplevel_state, bool enabled);
void wroc_xdg_toplevel_flush_configure(wroc_xdg_toplevel*);

// -----------------------------------------------------------------------------

enum class wroc_wl_buffer_type
{
    shm,
    dma,
};

struct wroc_wl_buffer : wrei_ref_counted
{
    wroc_server* server;

    wroc_wl_buffer_type type;

    wl_resource* wl_buffer;

    wrei_vec2i32 extent;

    wrei_ref<wren_image> image;

    bool locked = false;

    void lock();
    void unlock();

    virtual void on_commit() = 0;
};

// -----------------------------------------------------------------------------

struct wroc_wl_shm : wrei_ref_counted
{
    wroc_server* server;

    wl_resource* wl_shm;
};

struct wroc_wl_shm_pool : wrei_ref_counted
{
    wroc_server* server;

    wl_resource* wl_shm_pool;

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

struct wroc_zwp_linux_buffer_params : wrei_ref_counted
{
    wroc_server* server;

    wl_resource* zwp_linux_buffer_params_v1;

    wren_dma_params params;

    ~wroc_zwp_linux_buffer_params();
};

struct wroc_dma_buffer : wroc_wl_buffer
{
    virtual void on_commit() final override;
};

// -----------------------------------------------------------------------------

struct wroc_seat
{
    wroc_server* server;

    struct wroc_keyboard* keyboard;
    struct wroc_pointer*  pointer;

    std::string name;

    std::vector<wl_resource*> wl_seat;
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

struct wroc_keyboard
{
    wroc_server* server;

    std::vector<wl_resource*> wl_keyboard;
    wl_resource* focused;

    struct xkb_context* xkb_context;
    struct xkb_state*   xkb_state;
    struct xkb_keymap*  xkb_keymap;

    std::array<std::pair<wroc_modifiers, xkb_mod_mask_t>, std::size(wroc_modifier_xkb_names)> xkb_mod_masks;
    wroc_modifiers active_modifiers;

    int keymap_fd = -1;
    i32 keymap_size;

    i32 rate;
    i32 delay;
};

void wroc_keyboard_added(wroc_keyboard*);
void wroc_keyboard_keymap_update(wroc_keyboard*);
void wroc_keyboard_key(wroc_keyboard*, u32 keycode, bool pressed);
void wroc_keyboard_modifiers(wroc_keyboard*, u32 mods_depressed, u32 mods_latched, u32 mods_locked, u32 group);

wroc_modifiers wroc_keyboard_get_active_modifiers(wroc_keyboard*);

// -----------------------------------------------------------------------------

struct wroc_pointer
{
    wroc_server* server;

    std::vector<wl_resource*> wl_pointer;
    wl_resource* focused;
    wrei_weak<wroc_surface> focused_surface;

    wrei_vec2f64 layout_position;

    ~wroc_pointer()
    {
        log_error("POINTER DESTROYED: {}", (void*)this);
    }
};

void wroc_pointer_added(   wroc_pointer*);
void wroc_pointer_button(  wroc_pointer*, u32 button, bool pressed);
void wroc_pointer_absolute(wroc_pointer*, wroc_output*, wrei_vec2f64 pos);
void wroc_pointer_relative(wroc_pointer*, wrei_vec2f64 rel);
void wroc_pointer_axis(    wroc_pointer*, wrei_vec2f64 rel);

// -----------------------------------------------------------------------------

struct wroc_renderer
{
    wroc_server* server;

    wrei_ref<wren_context> wren;

    wrei_ref<wren_image> image;
};

void wroc_renderer_create( wroc_server*);
void wroc_renderer_destroy(wroc_server*);

enum class wroc_interaction_mode
{
    normal,
    move,
    size,
};

enum class wroc_edges
{
    left,
    top,
    right,
    bottom,
};
WREI_DECORATE_FLAG_ENUM(wroc_edges)

struct wroc_server
{
    wroc_backend*  backend;
    wroc_renderer* renderer;
    wroc_seat*     seat;

    wroc_modifiers main_mod = wroc_modifiers::alt;

    std::chrono::steady_clock::time_point epoch;

    wl_display* display;
    wl_event_loop* event_loop;

    std::vector<wroc_surface*> surfaces;
    wrei_weak<wroc_xdg_toplevel> toplevel_under_cursor;

    wroc_interaction_mode interaction_mode;

    struct {
        wrei_weak<wroc_xdg_toplevel> grabbed_toplevel;
        wrei_vec2f64 pointer_grab;
        wrei_vec2f64 surface_grab;
        wroc_edges edges;
    } movesize;
};

u32 wroc_get_elapsed_milliseconds(wroc_server*);
wroc_modifiers wroc_get_active_modifiers(wroc_server*);
