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

struct wroc_output_mode
{
    wl_output_mode flags;
    vec2i32 size;
    f32 refresh;
};

struct wroc_output : wrei_object
{
    wroc_server* server;

    wl_global* global;
    wroc_wl_resource_list resources;

    vec2i32 size;

    VkSurfaceKHR vk_surface;
    VkSemaphore timeline;
    u64 timeline_value = 0;
    VkSurfaceFormatKHR format;
    vkwsi_swapchain* swapchain;

    vec2i32 position;
    i32 scale = 1;

    vec2i32 physical_size_mm;
    wl_output_subpixel subpixel_layout = WL_OUTPUT_SUBPIXEL_UNKNOWN;
    std::string make;
    std::string model;
    std::string name;
    std::string description;
    wroc_output_mode mode;
};

vkwsi_swapchain_image wroc_output_acquire_image(wroc_output*);

void wroc_backend_output_create(wroc_backend*);
void wroc_backend_output_destroy(wroc_output*);

// -----------------------------------------------------------------------------

struct wroc_wl_region : wrei_object
{
    wroc_server* server;

    wroc_wl_resource resource;

    wrei_region region;
};

struct wroc_wl_buffer;

// -----------------------------------------------------------------------------

struct wroc_surface_role_addon : wrei_object
{
    virtual void on_commit() = 0;
    virtual void on_ack_configure(u32 serial) {}
    virtual bool is_synchronized() { return false; }
};

enum class wroc_surface_committed_state : u32
{
    none,
    buffer        = 1 << 0,
    offset        = 1 << 1,
    input_region  = 1 << 2,
    buffer_scale  = 1 << 3,
    surface_stack = 1 << 4,
};
WREI_DECORATE_FLAG_ENUM(wroc_surface_committed_state)

struct wroc_surface;

struct wroc_surface_state
{
    wroc_surface_committed_state committed;

    ref<wroc_wl_buffer> buffer;
    wroc_wl_resource_list frame_callbacks;
    vec2i32 offset;
    wrei_region input_region;
    f64 buffer_scale;
    std::vector<weak<wroc_surface>> surface_stack;
};

struct wroc_surface : wrei_object
{
    wroc_server* server = {};

    wroc_wl_resource resource;

    wroc_surface_state pending;
    wroc_surface_state current = {
        .input_region = wrei_region({{0, 0}, {INT32_MAX, INT32_MAX}}),
        .buffer_scale = 1.f
    };

    weak<wroc_surface_role_addon> role_addon;

    weak<wroc_output> output;

    vec2i32 position;

    ~wroc_surface();
};

void wroc_surface_commit(wroc_surface*);
bool wroc_surface_point_accepts_input(wroc_surface*, vec2f64 point);
void wroc_surface_set_output(wroc_surface*, wroc_output*);

// -----------------------------------------------------------------------------

enum class wroc_subsurface_committed_state : u32
{
    none,
    position,
};
WREI_DECORATE_FLAG_ENUM(wroc_subsurface_committed_state)

struct wroc_subsurface_state
{
    wroc_subsurface_committed_state committed;
    vec2i32 position;
};

struct wroc_subsurface : wroc_surface_role_addon
{
    ref<wroc_surface> surface;
    weak<wroc_surface> parent;

    wroc_wl_resource resource;

    wroc_subsurface_state pending;
    wroc_subsurface_state current;

    bool synchronized = true;

    void on_parent_commit();
    virtual void on_commit() final override;
    virtual bool is_synchronized() final override;

    static
    wroc_subsurface* try_from(wroc_surface* surface)
    {
        return surface ? dynamic_cast<wroc_subsurface*>(surface->role_addon.get()) : nullptr;
    }
};

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

struct wroc_xdg_surface : wroc_surface_role_addon
{
    ref<wroc_surface> surface;

    wroc_wl_resource resource;

    weak<wroc_surface_role_addon> xdg_role_addon;

    wrox_xdg_surface_state pending;
    wrox_xdg_surface_state current;

    struct {
        vec2i32 position;
        vec2i32 relative;
    } anchor;

    u32 sent_configure_serial = {};
    u32 acked_configure_serial = {};

    virtual void on_commit() final override;

    static
    wroc_xdg_surface* try_from(wroc_surface* surface)
    {
        return surface ? dynamic_cast<wroc_xdg_surface*>(surface->role_addon.get()) : nullptr;
    }
};

rect2i32 wroc_xdg_surface_get_geometry(wroc_xdg_surface* surface);
vec2i32  wroc_xdg_surface_get_position(wroc_xdg_surface* surface, rect2i32* p_geom = nullptr);
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

struct wroc_xdg_toplevel : wroc_surface_role_addon
{
    ref<wroc_xdg_surface> base;

    wroc_wl_resource resource;

    wroc_xdg_toplevel_state pending;
    wroc_xdg_toplevel_state current;

    bool initial_configure_complete;
    bool initial_size_receieved;

    vec2i32 bounds;
    vec2i32 size;
    std::vector<xdg_toplevel_state> states;
    wroc_xdg_toplevel_configure_state pending_configure = {};

    virtual void on_commit() final override;
    virtual void on_ack_configure(u32 serial) final override;

    static
    wroc_xdg_toplevel* try_from(wroc_xdg_surface* xdg_surface)
    {
        return xdg_surface ? dynamic_cast<wroc_xdg_toplevel*>(xdg_surface->xdg_role_addon.get()) : nullptr;
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

struct wroc_axis_region
{
    i32 pos;
    i32 size;
};

struct wroc_axis_overlaps
{
    i32 start;
    i32 end;
};

struct wroc_xdg_positioner_rules
{
    vec2i32 size;
    rect2i32 anchor_rect;
    xdg_positioner_anchor anchor;
    xdg_positioner_gravity gravity;
    xdg_positioner_constraint_adjustment constraint_adjustment;
    vec2i32 offset;
    bool reactive = false;
    vec2i32 parent_size;
    u32 parent_configure;
};

struct wroc_xdg_positioner : wrei_object
{
    wroc_server* server;

    wroc_wl_resource resource;

    wroc_xdg_positioner_rules rules;
};

// -----------------------------------------------------------------------------

struct wroc_xdg_popup : wroc_surface_role_addon
{
    ref<wroc_xdg_surface> base;

    wroc_wl_resource resource;

    ref<wroc_xdg_positioner> positioner;
    weak<wroc_xdg_surface> parent;
    weak<wroc_xdg_toplevel> root_toplevel;
    bool initial_configure_complete;

    virtual void on_commit() final override;

    static
    wroc_xdg_popup* try_from(wroc_xdg_surface* xdg_surface)
    {
        return xdg_surface ? dynamic_cast<wroc_xdg_popup*>(xdg_surface->xdg_role_addon.get()) : nullptr;
    }

    static
    wroc_xdg_popup* try_from(wroc_surface* surface)
    {
        return try_from(wroc_xdg_surface::try_from(surface));
    }
};

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

    wroc_wl_resource resource;

    vec2i32 extent;

    ref<wren_image> image;

    bool locked = false;

    void lock();
    void unlock();

    virtual void on_commit() = 0;
};

// -----------------------------------------------------------------------------

struct wroc_wl_shm_pool : wrei_object
{
    wroc_server* server;

    wroc_wl_resource resource;

    i32 size;
    int fd;
    void* data;

    ~wroc_wl_shm_pool();
};

struct wroc_shm_buffer : wroc_wl_buffer
{
    ref<wroc_wl_shm_pool> pool;

    i32 offset;
    i32 stride;
    wl_shm_format format;

    virtual void on_commit() final override;
};

// -----------------------------------------------------------------------------

struct wroc_zwp_linux_buffer_params : wrei_object
{
    wroc_server* server;

    wroc_wl_resource resource;

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

    wroc_wl_resource_list resources;
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

    wroc_wl_resource_list resources;
    weak<wroc_surface> focused_surface;

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

    wroc_wl_resource_list resources;
    weak<wroc_surface> focused_surface;

    std::vector<u32> pressed = {};

    vec2f64 layout_position;
};

// -----------------------------------------------------------------------------

WREI_DECORATE_FLAG_ENUM(wl_data_device_manager_dnd_action)

struct wroc_data_offer;
struct wroc_data_device;

struct wroc_data_source : wrei_object
{
    wroc_server* server;

    std::vector<std::string> mime_types;
    wl_data_device_manager_dnd_action dnd_actions;
    bool cancelled = false;

    wroc_wl_resource resource;

    ~wroc_data_source();
};

struct wroc_data_offer : wrei_object
{
    wroc_server* server;

    wroc_wl_resource resource;

    weak<wroc_data_source> source;
    weak<wroc_data_device> device;

    wl_data_device_manager_dnd_action action = WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE;
    std::string mime_type;

    ~wroc_data_offer();
};

struct wroc_data_device : wrei_object
{
    wroc_server* server;
    wroc_seat* seat;

    wroc_wl_resource resource;

    ~wroc_data_device();
};

void wroc_data_manager_offer_selection(wroc_server*, wl_client*);
void wroc_data_manager_update_drag(wroc_server*, wroc_surface*);
void wroc_data_manager_finish_drag(wroc_server*);

// -----------------------------------------------------------------------------

struct wroc_renderer : wrei_object
{
    wroc_server* server;

    ref<wren_context> wren;

    VkFormat output_format = VK_FORMAT_B8G8R8A8_UNORM;

    VkPipeline pipeline;

    ref<wren_image> image;
    ref<wren_sampler> sampler;

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

enum class wroc_directions : u32
{
    horizontal = 1 << 0,
    vertical   = 1 << 1,
};
WREI_DECORATE_FLAG_ENUM(wroc_directions);

struct wroc_server : wrei_object
{
    wroc_backend*      backend;
    ref<wroc_renderer> renderer;
    ref<wroc_seat>     seat;

    wroc_modifiers main_mod = wroc_modifiers::alt;

    std::chrono::steady_clock::time_point epoch;

    wl_display*    display;
    wl_event_loop* event_loop;

    std::vector<wroc_output*>  outputs;
    std::vector<wroc_surface*> surfaces;

    weak<wroc_xdg_toplevel> toplevel_under_cursor;
    weak<wroc_surface>      surface_under_cursor;
    weak<wroc_surface>      implicit_grab_surface;

    wroc_interaction_mode interaction_mode;

    // TODO: We should track these per client-pointer
    weak<wroc_surface> cursor_surface;
    vec2i32            cursor_hotspot;

    struct {
        weak<wroc_xdg_toplevel> grabbed_toplevel;
        vec2f64 pointer_grab;
        vec2i32 surface_grab;
        wroc_directions directions;
    } movesize;

    struct {
        std::vector<wroc_data_device*> devices;
        std::vector<wroc_data_source*> sources;
        weak<wroc_data_source> selection;

        struct {
            weak<wroc_data_device> device;
            weak<wroc_data_source> source;
            weak<wroc_surface>     icon;
            weak<wroc_surface>     offered_surface;
            weak<wroc_data_offer>  offer;
        } drag;
    } data_manager;
};

u32 wroc_get_elapsed_milliseconds(wroc_server*);
wroc_modifiers wroc_get_active_modifiers(wroc_server*);

void wroc_begin_move_interaction(wroc_xdg_toplevel*, wroc_pointer*, wroc_directions);
void wroc_begin_resize_interaction(wroc_xdg_toplevel*, wroc_pointer*, vec2i32 anchor_rel, wroc_directions);
