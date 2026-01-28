#pragma once

#include "wroc/wroc.hpp"

// -----------------------------------------------------------------------------

struct wroc_wayland_backend;

enum class wroc_wayland_buffer_state : u32
{
    released,
    acquired,
};

struct wroc_wayland_buffer : wrei_object
{
    struct wl_buffer* wl_buffer;
    ref<wren_image> image;

    wroc_wayland_buffer_state state;

    ~wroc_wayland_buffer();
};

struct wroc_wayland_buffer_slot
{
    ref<wren_semaphore> timeline;
    struct wp_linux_drm_syncobj_timeline_v1* timeline_proxy;
    u64 release_point;

    ref<wroc_wayland_buffer> buffer;
};

struct wroc_wayland_output : wroc_output
{
    std::vector<ref<wroc_wayland_buffer>> buffers;
    std::vector<wroc_wayland_buffer_slot> present_slots;
    wp_linux_drm_syncobj_surface_v1* syncobj_surface;

    struct wl_surface* wl_surface = {};
    struct xdg_surface* xdg_surface = {};
    xdg_toplevel* toplevel = {};
    zxdg_toplevel_decoration_v1* decoration = {};
    zwp_locked_pointer_v1* locked_pointer = {};
    bool locked = false;

    wl_callback* frame_callback = {};

    ~wroc_wayland_output();

    virtual wren_image* acquire() final override;
    virtual void present(wren_image*, wren_syncpoint) final override;
};

struct wroc_wayland_keyboard : wroc_keyboard
{
    struct wl_keyboard* wl_keyboard = {};

    ~wroc_wayland_keyboard();
};

struct wroc_wayland_pointer : wroc_pointer
{
    struct wl_pointer* wl_pointer = {};
    u32 last_serial = {};

    struct zwp_relative_pointer_v1* relative_pointer = {};
    weak<wroc_wayland_output> current_output = {};

    ~wroc_wayland_pointer();
};

struct wroc_wayland_semaphore_proxy
{
    weak<wren_semaphore> semaphore;
    struct wp_linux_drm_syncobj_timeline_v1* proxy;
};

struct wroc_wayland_backend : wroc_backend
{
    struct wl_display* wl_display = {};
    struct wl_registry* wl_registry = {};
    struct wl_compositor* wl_compositor;
    struct xdg_wm_base* xdg_wm_base = {};
    struct zxdg_decoration_manager_v1* zxdg_decoration_manager_v1 = {};
    struct zwp_relative_pointer_manager_v1* zwp_relative_pointer_manager_v1 = {};
    struct zwp_pointer_constraints_v1* zwp_pointer_constraints_v1 = {};

    struct zwp_linux_dmabuf_v1* zwp_linux_dmabuf_v1 = {};
    struct wp_linux_drm_syncobj_manager_v1* wp_linux_drm_syncobj_manager_v1 = {};
    std::vector<wroc_wayland_semaphore_proxy> syncobj_cache;

    struct wl_seat* wl_seat = {};

    u32 next_window_id = 1;
    std::vector<ref<wroc_wayland_output>> outputs;

    ref<wroc_wayland_keyboard> keyboard = {};
    ref<wroc_wayland_pointer>  pointer = {};

    ref<wrei_event_source> event_source;

    virtual void init() final override;
    virtual void start() final override;

    virtual void create_output() final override;
    virtual void destroy_output(wroc_output*) final override;

    ~wroc_wayland_backend();
};

wroc_wayland_output* wroc_wayland_backend_find_output_for_surface(wroc_wayland_backend*, wl_surface*);

void wroc_wayland_backend_update_pointer_constraint(wroc_wayland_output*);

extern const xdg_surface_listener wroc_xdg_surface_listener;
extern const xdg_wm_base_listener wroc_xdg_wm_base_listener;
extern const wl_pointer_listener wroc_wl_pointer_listener;
extern const wl_keyboard_listener wroc_wl_keyboard_listener;
extern const wl_seat_listener wroc_wl_seat_listener;
extern const wl_registry_listener wroc_wl_registry_listener;
extern const zxdg_toplevel_decoration_v1_listener wroc_zxdg_toplevel_decoration_v1_listener;
extern const xdg_toplevel_listener wroc_xdg_toplevel_listener;
extern const zwp_relative_pointer_v1_listener wroc_zwp_relative_pointer_v1_listener;
extern const zwp_locked_pointer_v1_listener wroc_zwp_locked_pointer_v1_listener;
