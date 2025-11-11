#pragma once

#include "wayland-server-core.h"
#include "wayland-server-protocol.h"
#include "xdg-shell-protocol.h"

#include "common/log.hpp"

#define INTERFACE_STUB [](auto...){}

template<typename T>
T* get_userdata(wl_resource* resource)
{
    return static_cast<T*>(wl_resource_get_user_data(resource));
}

inline
void debug_track_resource(wl_resource* resource)
{
    static i64 count = 0;
    log_trace("wl_resource ++ {}", ++count);

    auto* destroy_listener = new wl_listener {};
    destroy_listener->notify = [](wl_listener* listener, void*) {
        log_trace("wl_resource -- {}", --count);
        wl_list_remove(&listener->link);
        delete listener;
    };
    wl_resource_add_destroy_listener(resource, destroy_listener);
}

#define SIMPLE_RESOURCE_UNREF(Type, Member) \
    [](wl_resource* resource) { \
        if (auto* t = get_userdata<Type>(resource)) { \
            t->Member = nullptr; \
            unref(t); \
        } \
    }

template<typename T>
wl_array to_array(std::span<T> span)
{
    return wl_array {
        .size = span.size_bytes(),
        .alloc = span.size_bytes(),
        .data = const_cast<void*>(static_cast<const void*>(span.data())),
    };
}

extern const struct wl_compositor_interface impl_wl_compositor;
extern const struct wl_surface_interface    impl_wl_surface;

extern const struct xdg_wm_base_interface   impl_xdg_wm_base;
extern const struct xdg_surface_interface   impl_xdg_surface;
extern const struct xdg_toplevel_interface  impl_xdg_toplevel;

extern const struct wl_shm_interface        impl_wl_shm;
extern const struct wl_shm_pool_interface   impl_wl_shm_pool;
extern const struct wl_buffer_interface     impl_wl_buffer_for_shm;

extern const struct wl_seat_interface       impl_wl_seat;
extern const struct wl_keyboard_interface   impl_wl_keyboard;
extern const struct wl_pointer_interface    impl_wl_pointer;

extern const wl_global_bind_func_t bind_wl_compositor;
extern const wl_global_bind_func_t bind_wl_shm;
extern const wl_global_bind_func_t bind_xdg_wm_base;
extern const wl_global_bind_func_t bind_wl_seat;