#pragma once

#include "common/pch.hpp"
#include "common/types.hpp"
#include "common/util.hpp"
#include "common/log.hpp"

#include <vk-wsi.h>

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#include "renderer/vulkan_helpers.hpp"

// -----------------------------------------------------------------------------

struct Server;

void server_run(int argc, char* argv[]);
void server_terminate(Server*);

// -----------------------------------------------------------------------------

struct Renderer;

// -----------------------------------------------------------------------------

struct Backend;

void backend_init(Server*);
void backend_destroy(Backend*);

std::span<const char* const> backend_get_required_instance_extensions(Backend*);

// -----------------------------------------------------------------------------

struct Output
{
    Server* server;

    ivec2 size;

    VkSurfaceKHR vk_surface;
    VkSemaphore timeline;
    u64 timeline_value = 0;
    VkSurfaceFormatKHR format;
    vkwsi_swapchain* swapchain;
};

void output_added(Output*);
void output_removed(Output*);
void output_frame(Output*);

void output_init_swapchain(Output*);
vkwsi_swapchain_image output_acquire_image(Output*);

void backend_output_create(Backend*);
void backend_output_destroy(Output*);

// -----------------------------------------------------------------------------

struct XdgWmBase
{
    Server* server;

    struct wl_resource* xdg_wm_base;
};

struct Compositor
{
    Server* server;

    struct wl_resource* wl_compositor;
};

struct Surface
{
    Server* server;

    struct wl_resource* wl_surface;
    struct wl_resource* xdg_surface;
    struct wl_resource* xdg_toplevel;

    struct wl_resource* frame_callback;

    bool initial_commit = true;

    struct Buffer* current_buffer;
    VulkanImage current_image;

    ~Surface();
};

// -----------------------------------------------------------------------------

enum class BufferType
{
    shm,
};

struct Buffer
{
    Server* server;

    BufferType type;

    struct wl_resource* wl_buffer;
};

// -----------------------------------------------------------------------------

struct Shm
{
    Server* server;

    struct wl_resource* wl_shm;
};

struct ShmPool
{
    Server* server;

    struct wl_resource* wl_shm_pool;

    i32 size;
    int fd;
    void* data;

    ~ShmPool();
};

struct ShmBuffer : Buffer
{
    ShmPool* pool;

    i32 offset;
    i32 width;
    i32 height;
    i32 stride;
    wl_shm_format format;
};

// -----------------------------------------------------------------------------

struct Keyboard
{
    Server* server;

    struct xkb_context* xkb_context;
    struct xkb_state*   xkb_state;
    struct xkb_keymap*  xkb_keymap;

    i32 rate;
    i32 delay;
};

void keyboard_added(Keyboard*);
void keyboard_key(  Keyboard*, u32 keycode, bool pressed);

// -----------------------------------------------------------------------------

struct Pointer
{
    Server* server;
};

void pointer_added(   Pointer*);
void pointer_button(  Pointer*, u32 button, bool pressed);
void pointer_absolute(Pointer*, Output*, vec2 pos);
void pointer_relative(Pointer*, vec2 rel);
void pointer_axis(    Pointer*, vec2 rel);

// -----------------------------------------------------------------------------

struct Server
{
    Backend*  backend;
    Renderer* renderer;

    std::chrono::steady_clock::time_point epoch;

    wl_display* display;
    wl_event_loop* event_loop;

    std::vector<Surface*> surfaces;
};

u32 server_get_elapsed_milliseconds(Server*, std::chrono::steady_clock::time_point);

inline
Surface::~Surface()
{
    std::erase(server->surfaces, this);
}
