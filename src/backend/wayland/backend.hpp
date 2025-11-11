#pragma once

#define VK_USE_PLATFORM_WAYLAND_KHR
#include "renderer/vulkan_include.hpp"
#include "vulkan/vulkan_wayland.h"

#include "compositor/server.hpp"

#include <wayland-client-core.h>
#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"

// -----------------------------------------------------------------------------

template<typename T>
std::span<T> to_span(wl_array* array)
{
    usz count = array->size / sizeof(T);
    return std::span<T>(static_cast<T*>(array->data), count);
}

// -----------------------------------------------------------------------------

struct WaylandOutput : Output
{
    struct wl_surface* wl_surface;
    struct xdg_surface* xdg_surface;
    xdg_toplevel* toplevel;
    zxdg_toplevel_decoration_v1* decoration;
};

struct WaylandKeyboard : Keyboard
{
    struct wl_keyboard* wl_keyboard;

    std::array<bool, 256> pressed = {};
};

struct WaylandPointer : Pointer
{
    struct wl_pointer* wl_pointer;

    WaylandOutput* current_output;
};

struct Backend
{
    Server* server;

    struct wl_display* wl_display;
    struct wl_registry* wl_registry;
    struct wl_compositor* wl_compositor;
    struct xdg_wm_base* xdg_wm_base;
    struct zxdg_decoration_manager_v1* decoration_manager;

    struct wl_seat* seat;

    std::vector<WaylandOutput*> outputs;

    WaylandKeyboard* keyboard;
    WaylandPointer*  pointer;

    PFN_vkCreateWaylandSurfaceKHR vkCreateWaylandSurfaceKHR;

    wl_event_source* event_source;
};

WaylandOutput* backend_find_output_for_surface(Backend*, wl_surface*);

namespace listeners
{
    extern const xdg_surface_listener xdg_surface;
    extern const xdg_wm_base_listener xdg_wm_base;
    extern const wl_pointer_listener wl_pointer;
    extern const wl_keyboard_listener wl_keyboard;
    extern const wl_seat_listener wl_seat;
    extern const wl_registry_listener wl_registry;
    extern const zxdg_toplevel_decoration_v1_listener zxdg_toplevel_decoration_v1;
    extern const xdg_toplevel_listener xdg_toplevel;
}
