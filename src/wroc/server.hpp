#pragma once

#include "protocol.hpp"
#include "util.hpp"

#include "wrei/pch.hpp"
#include "wrei/types.hpp"
#include "wrei/util.hpp"
#include "wrei/log.hpp"

#include "wren/wren.hpp"

#define WROC_NOISY_FRAME_TIME 0

struct wroc_server;
struct wren_renderer;
struct wroc_output;
struct wroc_surface;
struct wroc_buffer;
struct wroc_cursor_surface;
struct wroc_seat_keyboard;
struct wroc_seat_pointer;
struct wroc_pointer_constraint;
struct wroc_data_offer;
struct wroc_data_device;

// -----------------------------------------------------------------------------

struct wroc_server;
void wroc_run(int argc, char* argv[]);
void wroc_terminate(wroc_server*);

// -----------------------------------------------------------------------------

struct wroc_backend : wrei_object
{
    virtual void create_output() = 0;
    virtual void destroy_output(wroc_output*) = 0;
};

enum struct wroc_backend_type
{
    wayland,
    direct,
};

void wroc_backend_init(wroc_server*, wroc_backend_type);

// -----------------------------------------------------------------------------

enum class wroc_output_mode_flags
{
    none,

    current   = 1 << 0,
    preferred = 1 << 1,
};
WREI_DECORATE_FLAG_ENUM(wroc_output_mode_flags)

struct wroc_output_mode
{
    wroc_output_mode_flags flags;
    vec2i32 size;
    f64 refresh;
};

/*
 * Describes an output state. Only represents a current state,
 */
struct wroc_output_desc
{
    std::string make;
    std::string model;
    std::string name;
    std::string description;
    vec2f64 physical_size_mm;
    wl_output_subpixel subpixel = WL_OUTPUT_SUBPIXEL_UNKNOWN;
    wl_output_transform transform = WL_OUTPUT_TRANSFORM_NORMAL;
    f64 scale;

    std::vector<wroc_output_mode> modes;
};

/*
 * Represents a wl_output protocol object. This may or may
 * not correspond to any actual output made available by the backend.
 */
struct wroc_wl_output : wrei_object
{
    wroc_server* server;

    wl_global* global;
    wroc_resource_list resources;

    wroc_output_desc desc;
    vec2i32 position;
};

void wroc_output_desc_update(wroc_wl_output*);
void wroc_output_enter_surface(wroc_wl_output*, wroc_surface*);

/*
 * A backend output that can be displayed to
 */
struct wroc_output : wrei_object
{
    wroc_server* server;

    VkSurfaceKHR vk_surface;
    VkSemaphore timeline;
    u64 timeline_value = 0;
    VkSurfaceFormatKHR format;
    vkwsi_swapchain* swapchain;

    std::chrono::steady_clock::time_point acquire_time;
    std::chrono::steady_clock::time_point present_time;

    vec2i32 size;
    rect2f64 layout_rect;

    wroc_output_desc desc;

    ~wroc_output();
};

vkwsi_swapchain_image wroc_output_acquire_image(wroc_output*);

vec2i32 wroc_output_get_pixel(wroc_output*, vec2f64 global_pos, vec2f64* remainder = nullptr);
rect2i32 wroc_output_get_pixel_rect(wroc_output*, rect2f64 rect, rect2f64* remainder = nullptr);

struct wroc_output_layout : wrei_object
{
    wroc_server* server;

    // TODO: Support multi output description for clients
    ref<wroc_wl_output> primary;
    std::vector<weak<wroc_output>> outputs;
};

void wroc_output_layout_init(wroc_server*);
void wroc_output_layout_add_output(wroc_output_layout*, wroc_output*);
void wroc_output_layout_remove_output(wroc_output_layout*, wroc_output*);
vec2f64 wroc_output_layout_clamp_position(wroc_output_layout*, vec2f64 global_pos);

// -----------------------------------------------------------------------------

struct wroc_region : wrei_object
{
    wroc_server* server;

    wroc_resource resource;

    wrei_region region;
};

// -----------------------------------------------------------------------------

enum class wroc_surface_role
{
    none,
    cursor,
    drag_icon,
    subsurface,
    xdg_toplevel,
    xdg_popup,
};

enum class wroc_surface_commit_flags
{
    none,
    from_parent = 1 << 0,
};
WREI_DECORATE_FLAG_ENUM(wroc_surface_commit_flags)

struct wroc_surface_addon : wrei_object
{
    weak<wroc_surface> surface;

    virtual void on_commit(wroc_surface_commit_flags) = 0;
    virtual void on_ack_configure(u32 serial) {}
    virtual bool is_synchronized() { return false; }
    virtual wroc_surface_role get_role() { return wroc_surface_role::none; }
};

void wroc_surface_addon_detach(wroc_surface_addon* addon);
void wroc_surface_addon_destroy(wl_client*, wl_resource*);

enum class wroc_surface_committed_state : u32
{
    none,
    buffer        = 1 << 0,
    offset        = 1 << 1,
    opaque_region = 1 << 2,
    input_region  = 1 << 3,
    buffer_scale  = 1 << 4,
    surface_stack = 1 << 5,
};
WREI_DECORATE_FLAG_ENUM(wroc_surface_committed_state)

struct wroc_surface_state
{
    wroc_surface_committed_state committed;

    ref<wroc_buffer> buffer;
    wroc_resource_list frame_callbacks;
    vec2i32 delta;
    wrei_region opaque_region;
    wrei_region input_region;
    f64 buffer_scale;
    std::vector<weak<wroc_surface>> surface_stack;
};

struct wroc_surface : wrei_object
{
    wroc_server* server = {};

    wroc_resource resource;

    wroc_surface_state pending;
    wroc_surface_state cached;
    wroc_surface_state current = {
        .input_region = wrei_region({{0, 0}, {INT32_MAX, INT32_MAX}}),
        .buffer_scale = 1.f
    };

    wroc_surface_role role = wroc_surface_role::none;
    weak<wroc_surface_addon> role_addon;
    std::vector<ref<wroc_surface_addon>> addons;

    std::optional<weak<wroc_cursor_surface>> cursor;

    rect2i32 buffer_dst; // in surface coordinates, origin represents "offset" surface property
    rect2f64 buffer_src; // in buffer coordinates

    ~wroc_surface();
};

vec2f64 wroc_surface_get_position(wroc_surface* surface);

bool wroc_surface_point_accepts_input(wroc_surface*, vec2f64 surface_pos);
bool wroc_surface_is_synchronized(wroc_surface*);

void wroc_surface_raise(wroc_surface*);

bool wroc_surface_put_addon(wroc_surface*, wroc_surface_addon*);

wroc_surface_addon* wroc_surface_get_addon(wroc_surface*, const std::type_info&);
template<typename T>
T* wroc_surface_get_addon(wroc_surface* surface)
{
    return static_cast<T*>(wroc_surface_get_addon(surface, typeid(T)));
}

template<typename T>
T* wroc_surface_get_or_create_addon(wroc_surface* surface, bool* created = nullptr)
{
    if (created) *created = false;
    auto* existing = wroc_surface_get_addon<T>(surface);
    if (existing) return existing;
    auto addon = wrei_create<T>();
    if (!wroc_surface_put_addon(surface, addon.get())) return nullptr;
    if (created) *created = true;
    return addon.get();
}

// -----------------------------------------------------------------------------

enum class wroc_viewport_committed_state : u32
{
    none,

    source      = 1 << 0,
    destination = 1 << 1,
};
WREI_DECORATE_FLAG_ENUM(wroc_viewport_committed_state)

struct wroc_viewport_state
{
    wroc_viewport_committed_state committed;
    rect2f64 source;
    vec2i32 destination;
};

struct wroc_viewport : wroc_surface_addon
{
    weak<wroc_surface> parent;

    wroc_resource resource;

    wroc_viewport_state pending;
    wroc_viewport_state current;

    virtual void on_commit(wroc_surface_commit_flags) final override;
};

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

struct wroc_subsurface : wroc_surface_addon
{
    weak<wroc_surface> parent;

    wroc_resource resource;

    wroc_subsurface_state pending;
    wroc_subsurface_state current;

    bool synchronized = true;

    virtual void on_commit(wroc_surface_commit_flags) final override;
    virtual bool is_synchronized() final override;

    virtual wroc_surface_role get_role() final override { return wroc_surface_role::subsurface; }
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

struct wroc_xdg_surface : wroc_surface_addon
{
    wroc_resource resource;

    wrox_xdg_surface_state pending;
    wrox_xdg_surface_state current;

    struct {
        vec2f64 position;
        vec2i32 relative;
    } anchor;

    u32 sent_configure_serial = {};
    u32 acked_configure_serial = {};

    virtual void on_commit(wroc_surface_commit_flags) final override;
};

rect2i32 wroc_xdg_surface_get_geometry(wroc_xdg_surface*);
// vec2f64  wroc_xdg_surface_get_position(wroc_xdg_surface*, rect2i32* p_geom = nullptr);
void wroc_xdg_surface_flush_configure(wroc_xdg_surface*);

// -----------------------------------------------------------------------------

struct wroc_xdg_shell_role_addon : wroc_surface_addon
{
    wroc_xdg_surface* base() const { return wroc_surface_get_addon<wroc_xdg_surface>(surface.get()); }
};

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

struct wroc_toplevel : wroc_xdg_shell_role_addon
{
    wroc_resource resource;

    wroc_xdg_toplevel_state pending;
    wroc_xdg_toplevel_state current;

    bool initial_configure_complete;
    bool initial_size_receieved;

    vec2i32 bounds;
    vec2i32 size;
    std::vector<xdg_toplevel_state> states;
    wroc_xdg_toplevel_configure_state pending_configure = {};

    virtual void on_commit(wroc_surface_commit_flags) final override;
    virtual void on_ack_configure(u32 serial) final override;

    virtual wroc_surface_role get_role() final override { return wroc_surface_role::xdg_toplevel; }
};

void wroc_xdg_toplevel_set_bounds(wroc_toplevel*, vec2i32 bounds);
void wroc_xdg_toplevel_set_size(wroc_toplevel*, vec2i32 size);
void wroc_xdg_toplevel_set_state(wroc_toplevel*, xdg_toplevel_state, bool enabled);
void wroc_xdg_toplevel_flush_configure(wroc_toplevel*);

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

struct wroc_positioner_rules
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

struct wroc_positioner : wrei_object
{
    wroc_server* server;

    wroc_resource resource;

    wroc_positioner_rules rules;
};

// -----------------------------------------------------------------------------

struct wroc_popup : wroc_xdg_shell_role_addon
{
    wroc_resource resource;

    ref<wroc_positioner> positioner;
    std::optional<u32> reposition_token;

    weak<wroc_xdg_surface> parent;
    weak<wroc_toplevel> root_toplevel;
    bool initial_configure_complete;

    virtual void on_commit(wroc_surface_commit_flags) final override;

    virtual wroc_surface_role get_role() final override { return wroc_surface_role::xdg_popup; }
};

// -----------------------------------------------------------------------------

enum class wroc_buffer_type : u32
{
    shm,
    dma,
};

struct wroc_buffer : wrei_object
{
    wroc_server* server;

    wroc_buffer_type type;

    wroc_resource resource;

    vec2u32 extent;

    ref<wren_image> image;

    bool locked = false;

    void lock();
    void unlock();

    virtual void on_commit() = 0;
};

// -----------------------------------------------------------------------------

struct wroc_shm_pool : wrei_object
{
    wroc_server* server;

    wroc_resource resource;

    i32 size;
    int fd;
    void* data;

    ~wroc_shm_pool();
};

struct wroc_shm_buffer : wroc_buffer
{
    ref<wroc_shm_pool> pool;

    i32 offset;
    i32 stride;
    wren_format format;

    virtual void on_commit() final override;
};

// -----------------------------------------------------------------------------

struct wroc_dma_buffer_params : wrei_object
{
    wroc_server* server;

    wroc_resource resource;

    wren_dma_params params;

    ~wroc_dma_buffer_params();
};

struct wroc_dma_buffer : wroc_buffer
{
    virtual void on_commit() final override;
};

// -----------------------------------------------------------------------------

struct wroc_seat : wrei_object
{
    wroc_server* server;

    ref<wroc_seat_keyboard> keyboard;
    ref<wroc_seat_pointer>  pointer;

    std::string name;

    wroc_resource_list resources;
};

void wroc_seat_init(wroc_server*);
void wroc_seat_init_keyboard(wroc_seat*);
void wroc_seat_init_pointer(wroc_seat*);

// -----------------------------------------------------------------------------

enum class wroc_modifiers : u32
{
    none,

    mod    = 1 << 0,
    super  = 1 << 1,
    shift  = 1 << 2,
    ctrl   = 1 << 3,
    alt    = 1 << 4,
    num    = 1 << 5,
    caps   = 1 << 6,
};
WREI_DECORATE_FLAG_ENUM(wroc_modifiers)

enum class wroc_key_action
{
    press,      // Key was pressed by source
    release,    // Key was released by source
    enter,      // Key was "discovered" in the pressed state (does not trigger "on press" actions)
                // E.g. keyboard enter events in the Wayland client
};

/**
 * Represents a source (evdev) keyboard
 *
 * Source keyboards are "dumb" evdev (KEY_*) key code buckets.
 * We ignore pre-existing state (e.g. modifiers/keymaps) from sources (E.g. nested wayland environments)
 */
struct wroc_keyboard : wrei_object
{
    wroc_server* server;

    std::flat_set<u32> pressed = {};

    weak<wroc_seat_keyboard> target;

    void enter(std::span<const u32>);
    void leave();
    void press(u32);
    void release(u32);

    virtual void update_leds(libinput_led) {};

    ~wroc_keyboard();
};

/*
 * Represents a virtual (xkb) keyboard that is exposed to clients
 *
 * This will typically be an aggregate of several source keyboards
 * XKB keymaps are selected here, modifier states are derived based on source states/events
 * LED event states are sent back to source keyboards.
 */
struct wroc_seat_keyboard : wrei_object
{
    wroc_seat* seat;

    std::vector<wroc_keyboard*> sources;

    // Aggregate of sources[...]->pressed
    wrei_counting_set<u32> pressed;

    wroc_resource_list resources;
    weak<wroc_surface> focused_surface;

    struct xkb_context* context;
    struct xkb_state*   state;
    struct xkb_keymap*  keymap;

    wrei_enum_map<wroc_modifiers, xkb_mod_mask_t> mod_masks;

    int keymap_fd = -1;
    i32 keymap_size;

    i32 rate = 25;
    i32 delay = 600;

    void attach(wroc_keyboard*);

    bool is_locked(wroc_modifiers) const;
    void set_locked(wroc_modifiers, bool locked);

    ~wroc_seat_keyboard();
};

void wroc_seat_keyboard_on_get(wroc_seat_keyboard*, wl_client*, wl_resource*);

wroc_modifiers wroc_keyboard_get_active_modifiers(wroc_seat_keyboard*);

void wroc_keyboard_clear_focus(wroc_seat_keyboard*);
void wroc_keyboard_enter(wroc_seat_keyboard*, wroc_surface*);

// -----------------------------------------------------------------------------

struct wroc_pointer : wrei_object
{
    wroc_server* server;

    std::flat_set<u32> pressed = {};

    weak<wroc_seat_pointer> target;

    void enter(std::span<const u32>);
    void leave();
    void press(u32);
    void release(u32);

    void absolute(wroc_output*, vec2f64 offset);
    void relative(vec2f64 delta);

    void scroll(vec2f64 delta);

    ~wroc_pointer();
};

struct wroc_seat_pointer : wrei_object
{
    wroc_seat* seat;

    std::vector<wroc_pointer*> sources;

    // Aggregate of sources[...]->pressed
    wrei_counting_set<u32> pressed;

    wroc_resource_list resources;
    wroc_resource_list relative_pointers;
    weak<wroc_surface> focused_surface;

    vec2f64 position;

    weak<wroc_pointer_constraint> active_constraint;

    void attach(wroc_pointer*);
};

// -----------------------------------------------------------------------------

enum class wroc_pointer_constraint_committed_state
{
    none,

    position_hint = 1 << 0,
    region        = 1 << 1,
    region_unset  = 1 << 2,
};
WREI_DECORATE_FLAG_ENUM(wroc_pointer_constraint_committed_state)

struct wroc_pointer_constraint_state
{
    wroc_pointer_constraint_committed_state committed;
    vec2f64 position_hint;
    wrei_region region;
};

enum class wroc_pointer_constraint_type
{
    locked,
    confined,
};

struct wroc_pointer_constraint : wroc_surface_addon
{
    wroc_resource resource;

    wroc_pointer_constraint_type type;
    weak<wroc_seat_pointer> pointer;
    zwp_pointer_constraints_v1_lifetime lifetime;

    wroc_pointer_constraint_state pending;
    wroc_pointer_constraint_state current;

    virtual void on_commit(wroc_surface_commit_flags) final override;

    void activate();
    void deactivate();

    ~wroc_pointer_constraint();
};

// -----------------------------------------------------------------------------

WREI_DECORATE_FLAG_ENUM(wl_data_device_manager_dnd_action)

struct wroc_data_source : wrei_object
{
    wroc_server* server;

    std::vector<std::string> mime_types;
    wl_data_device_manager_dnd_action dnd_actions;
    bool cancelled = false;

    wroc_resource resource;

    ~wroc_data_source();
};

struct wroc_data_offer : wrei_object
{
    wroc_server* server;

    wroc_resource resource;

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

    wroc_resource resource;

    ~wroc_data_device();
};

void wroc_data_manager_offer_selection(wroc_server*, wl_client*);
void wroc_data_manager_update_drag(wroc_server*, wroc_surface*);
void wroc_data_manager_finish_drag(wroc_server*);

// -----------------------------------------------------------------------------

struct wroc_drag_icon : wroc_surface_addon
{
    virtual void on_commit(wroc_surface_commit_flags) final override;

    virtual wroc_surface_role get_role() final override { return wroc_surface_role::drag_icon; }
};

// -----------------------------------------------------------------------------

struct wroc_cursor_surface : wroc_surface_addon
{
    virtual void on_commit(wroc_surface_commit_flags) final override;

    virtual wroc_surface_role get_role() final override { return wroc_surface_role::cursor; }
};

struct wroc_cursor : wrei_object
{
    wroc_server* server;

    struct {
        ref<wren_image> image;
        vec2i32         hotspot;
    } fallback;
};

void wroc_cursor_create(wroc_server* server);
void wroc_cursor_set(wroc_cursor*, wl_client* client, wroc_surface* cursor_surface, vec2i32 hotspot);

// -----------------------------------------------------------------------------

enum class wroc_render_options
{
    none,

    no_dmabuf      = 1 << 0,
    separate_draws = 1 << 1,
};
WREI_DECORATE_FLAG_ENUM(wroc_render_options)

struct wroc_renderer : wrei_object
{
    wroc_server* server;

    struct {
        int format_table;
        usz format_table_size;
        std::vector<u16> tranche_formats;
    } buffer_feedback;

    ref<wren_context> wren;

    wroc_render_options options;

    wren_format output_format = wren_format_from_drm(DRM_FORMAT_ARGB8888);

    ref<wren_pipeline> pipeline;

    wren_array<struct wroc_shader_rect> rects;
    std::vector<struct wroc_shader_rect> rects_cpu;

    ref<wren_image> background;
    ref<wren_sampler> sampler;

    bool show_debug_cursor = false;

    bool screenshot_queued = false;
};

void wroc_renderer_create(wroc_server*, wroc_render_options);
void wroc_renderer_init_buffer_feedback(wroc_renderer*);
void wroc_render_frame(wroc_output*);

// -----------------------------------------------------------------------------

struct wroc_imgui : wrei_object
{
    wroc_server* server;

    std::chrono::steady_clock::time_point last_frame = {};

    ImGuiContext* context;

    weak<wroc_output> output;

    ref<wren_pipeline> pipeline;

    wren_array<ImDrawIdx> indices;
    wren_array<ImDrawVert> vertices;

    ref<wren_image> font_image;

    bool wants_mouse;
    bool wants_keyboard;
};

struct alignas(u64) wroc_imgui_texture
{
    wren_image_handle<vec4f32> handle;
    wren_format format;

    wroc_imgui_texture(wren_image* image, wren_sampler* sampler)
        : handle(image, sampler)
        , format(image->format)
    {}

    operator ImTextureID() const { return std::bit_cast<ImTextureID>(*this); }
};

template<typename ...Args>
void ImGui_Text(std::format_string<Args...> fmt, Args&&... args)
{
    ImGui::TextUnformatted(std::vformat(fmt.get(), std::make_format_args(args...)).c_str());
}

void wroc_imgui_init(wroc_server*);
void wroc_imgui_frame(wroc_imgui*, vec2u32 extent, VkCommandBuffer);
bool wroc_imgui_handle_event(wroc_imgui*, const struct wroc_event&);

// -----------------------------------------------------------------------------

struct wroc_launcher;
WREI_OBJECT_EXPLICIT_DECLARE(wroc_launcher);

void wroc_launcher_init(wroc_server*);
void wroc_launcher_frame(wroc_launcher*, vec2u32 extent);
bool wroc_launcher_handle_event(wroc_launcher*, const struct wroc_event&);

// -----------------------------------------------------------------------------

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

enum class wroc_options : u32
{
    none,

    imgui = 1 << 0,
};
WREI_DECORATE_FLAG_ENUM(wroc_options)

struct wroc_server : wrei_object
{
    ref<wroc_backend>  backend;
    ref<wroc_renderer> renderer;
    ref<wroc_seat>     seat;

    ref<wroc_imgui> imgui;
    ref<wroc_launcher> launcher;

    wroc_options options;

    wroc_modifiers main_mod = wroc_modifiers::alt;

    std::chrono::steady_clock::time_point epoch;

    wl_display*    display;
    wl_event_loop* event_loop;

    ref<wroc_output_layout> output_layout;
    std::vector<wroc_surface*> surfaces;

    weak<wroc_surface>  implicit_grab_surface;

    wroc_interaction_mode interaction_mode;

    // TODO: We should track these per client-pointer
    ref<wroc_cursor> cursor;

    struct {
        weak<wroc_toplevel> grabbed_toplevel;
        vec2f64 pointer_grab;
        vec2f64 surface_grab;
        wroc_directions directions;
    } movesize;

    struct {
        std::vector<wroc_data_device*> devices;
        std::vector<wroc_data_source*> sources;
        weak<wroc_data_source> selection;

        struct {
            weak<wroc_data_device> device;
            weak<wroc_data_source> source;
            weak<wroc_drag_icon>   icon;
            weak<wroc_surface>     offered_surface;
            weak<wroc_data_offer>  offer;
        } drag;
    } data_manager;
};

wl_global* wroc_server_global(wroc_server* server, const wl_interface* interface, i32 version, wl_global_bind_func_t bind, void* data = nullptr);
#define WROC_SERVER_GLOBAL(Server, Interface, ...) \
    wroc_server_global(Server, &Interface##_interface, wroc_##Interface##_version, wroc_##Interface##_bind_global __VA_OPT__(,) __VA_ARGS__)

u32 wroc_get_elapsed_milliseconds(wroc_server*);
wroc_modifiers wroc_get_active_modifiers(wroc_server*);

void wroc_begin_move_interaction(wroc_toplevel*, wroc_seat_pointer*, wroc_directions);
void wroc_begin_resize_interaction(wroc_toplevel*, wroc_seat_pointer*, vec2i32 anchor_rel, wroc_directions);

wroc_surface* wroc_get_surface_under_cursor(wroc_server* server, wroc_toplevel** toplevel = nullptr);
