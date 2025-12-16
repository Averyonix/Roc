#pragma once

#include "wrei/log.hpp"
#include "wrei/ref.hpp"

template<typename T>
T* wroc_get_userdata(wl_resource* resource)
{
    if (!resource) return nullptr;
    auto* base = static_cast<wrei_object*>(wl_resource_get_user_data(resource));
    if (!base) return nullptr;
    auto* cast = dynamic_cast<T*>(base);
    if (!cast) {
        log_error("Fatal error casting wl_resource userdata: expected {} got {}", typeid(T).name(), typeid(*base).name());
        wrei_debugbreak();
    }
    return cast;
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

inline
void wroc_resource_simple_unref(wl_resource* resource)
{
    wrei_remove_ref(static_cast<wrei_object*>(wl_resource_get_user_data(resource)));
}

inline
void wroc_resource_set_implementation_refcounted(wl_resource* resource, const void* implementation, wrei_object* base)
{
    wl_resource_set_implementation(resource, implementation, base, wroc_resource_simple_unref);
}

inline
void wroc_resource_set_implementation(wl_resource* resource, const void* implementation, wrei_object* base)
{
    wl_resource_set_implementation(resource, implementation, base, nullptr);
}

inline
void wroc_simple_resource_destroy_callback(wl_client* client, wl_resource* resource)
{
    wl_resource_destroy(resource);
}

extern const struct wl_compositor_interface wroc_wl_compositor_impl;
extern const struct wl_region_interface     wroc_wl_region_impl;
extern const struct wl_surface_interface    wroc_wl_surface_impl;

extern const struct wl_subcompositor_interface wroc_wl_subcompositor_impl;
extern const struct wl_subsurface_interface    wroc_wl_subsurface_impl;

extern const struct wl_output_interface     wroc_wl_output_impl;

extern const struct xdg_wm_base_interface    wroc_xdg_wm_base_impl;
extern const struct xdg_surface_interface    wroc_xdg_surface_impl;
extern const struct xdg_toplevel_interface   wroc_xdg_toplevel_impl;
extern const struct xdg_positioner_interface wroc_xdg_positioner_impl;
extern const struct xdg_popup_interface      wroc_xdg_popup_impl;

extern const struct wl_buffer_interface     wroc_wl_buffer_impl;

extern const struct wl_shm_interface        wroc_wl_shm_impl;
extern const struct wl_shm_pool_interface   wroc_wl_shm_pool_impl;

extern const struct wl_seat_interface       wroc_wl_seat_impl;
extern const struct wl_keyboard_interface   wroc_wl_keyboard_impl;
extern const struct wl_pointer_interface    wroc_wl_pointer_impl;

extern const struct wl_data_device_manager_interface wroc_wl_data_device_manager_impl;
extern const struct wl_data_offer_interface          wroc_wl_data_offer_impl;
extern const struct wl_data_source_interface         wroc_wl_data_source_impl;
extern const struct wl_data_device_interface         wroc_wl_data_device_impl;

extern const struct zwp_linux_dmabuf_v1_interface          wroc_zwp_linux_dmabuf_v1_impl;
extern const struct zwp_linux_buffer_params_v1_interface   wroc_zwp_linux_buffer_params_v1_impl;
extern const struct zwp_linux_dmabuf_feedback_v1_interface wroc_zwp_linux_dmabuf_feedback_v1_impl;

extern const struct zwp_pointer_gestures_v1_interface      wroc_zwp_pointer_gestures_v1_impl;
extern const struct zwp_pointer_gesture_swipe_v1_interface wroc_zwp_pointer_gesture_swipe_v1_impl;
extern const struct zwp_pointer_gesture_pinch_v1_interface wroc_zwp_pointer_gesture_pinch_v1_impl;
extern const struct zwp_pointer_gesture_hold_v1_interface  wroc_zwp_pointer_gesture_hold_v1_impl;

extern const u32 wroc_wl_compositor_version;
extern const u32 wroc_wl_subcompositor_version;
extern const u32 wroc_wl_shm_version;
extern const u32 wroc_xdg_wm_base_version;
extern const u32 wroc_wl_seat_version;
extern const u32 wroc_wl_output_version;
extern const u32 wroc_wl_data_device_manager_version;
extern const u32 wroc_zwp_linux_dmabuf_v1_version;
extern const u32 wroc_zwp_pointer_gestures_v1_version;

void wroc_wl_compositor_bind_global(          wl_client*, void*, u32 version, u32 id);
void wroc_wl_subcompositor_bind_global(       wl_client*, void*, u32 version, u32 id);
void wroc_wl_shm_bind_global(                 wl_client*, void*, u32 version, u32 id);
void wroc_xdg_wm_base_bind_global(            wl_client*, void*, u32 version, u32 id);
void wroc_wl_seat_bind_global(                wl_client*, void*, u32 version, u32 id);
void wroc_wl_output_bind_global(              wl_client*, void*, u32 version, u32 id);
void wroc_wl_data_device_manager_bind_global( wl_client*, void*, u32 version, u32 id);
void wroc_zwp_linux_dmabuf_v1_bind_global(    wl_client*, void*, u32 version, u32 id);
void wroc_zwp_pointer_gestures_v1_bind_global(wl_client*, void*, u32 version, u32 id);
