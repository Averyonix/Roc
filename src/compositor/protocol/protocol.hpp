#pragma once

#include "wayland-server-core.h"
#include "wayland-server-protocol.h"
#include "xdg-shell-protocol.h"

extern const struct wl_compositor_interface impl_wl_compositor;
extern const struct wl_surface_interface    impl_wl_surface;
extern const struct xdg_wm_base_interface   impl_xdg_wm_base;
extern const struct xdg_surface_interface   impl_xdg_surface;
extern const struct xdg_toplevel_interface  impl_xdg_toplevel;
extern const struct wl_shm_interface        impl_wl_shm;
extern const struct wl_shm_pool_interface   impl_wl_shm_pool;
extern const struct wl_buffer_interface     impl_wl_buffer;

extern const wl_global_bind_func_t bind_wl_compositor;
extern const wl_global_bind_func_t bind_wl_shm;
extern const wl_global_bind_func_t bind_xdg_wm_base;
