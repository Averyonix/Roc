#pragma once

#include "wrei/log.hpp"

template<typename T>
T* wroc_get_userdata(wl_resource* resource)
{
    return static_cast<T*>(wl_resource_get_user_data(resource));
}

#define WROC_NOISY_WL_RESOURCE 0

inline
void wroc_debug_track_resource(wl_resource* resource)
{
#if WROC_NOISY_WL_RESOURCE
    static i64 count = 0;
    log_trace("wl_resource ++ {}", ++count);

    auto* destroy_listener = new wl_listener {};
    destroy_listener->notify = [](wl_listener* listener, void*) {
        log_trace("wl_resource -- {}", --count);
        wl_list_remove(&listener->link);
        delete listener;
    };
    wl_resource_add_destroy_listener(resource, destroy_listener);
#endif
}

#define WROC_SIMPLE_RESOURCE_UNREF(Type) \
    [](wl_resource* resource) { \
        auto* t = wroc_get_userdata<Type>(resource); \
        wrei_remove_ref(t); \
    }

inline
void wroc_simple_resource_destroy_callback(wl_client* client, wl_resource* resource)
{
    wl_resource_destroy(resource);
}

extern const struct wl_compositor_interface wroc_wl_compositor_impl;
extern const struct wl_region_interface     wroc_wl_region_impl;
extern const struct wl_surface_interface    wroc_wl_surface_impl;

extern const struct xdg_wm_base_interface   wroc_xdg_wm_base_impl;
extern const struct xdg_surface_interface   wroc_xdg_surface_impl;
extern const struct xdg_toplevel_interface  wroc_xdg_toplevel_impl;

extern const struct wl_buffer_interface     wroc_wl_buffer_impl;

extern const struct wl_shm_interface        wroc_wl_shm_impl;
extern const struct wl_shm_pool_interface   wroc_wl_shm_pool_impl;

extern const struct wl_seat_interface       wroc_wl_seat_impl;
extern const struct wl_keyboard_interface   wroc_wl_keyboard_impl;
extern const struct wl_pointer_interface    wroc_wl_pointer_impl;

extern const struct zwp_linux_dmabuf_v1_interface          wroc_zwp_linux_dmabuf_v1_impl;
extern const struct zwp_linux_buffer_params_v1_interface   wroc_zwp_linux_buffer_params_v1_impl;
extern const struct zwp_linux_dmabuf_feedback_v1_interface wroc_zwp_linux_dmabuf_feedback_v1_impl;

void wroc_wl_compositor_bind_global(      wl_client* client, void* data, u32 version, u32 id);
void wroc_wl_shm_bind_global(             wl_client* client, void* data, u32 version, u32 id);
void wroc_xdg_wm_base_bind_global(        wl_client* client, void* data, u32 version, u32 id);
void wroc_wl_seat_bind_global(            wl_client* client, void* data, u32 version, u32 id);
void wroc_zwp_linux_dmabuf_v1_bind_global(wl_client* client, void* data, u32 version, u32 id);
