#pragma once

#include "common/pch.hpp"
#include "common/types.hpp"
#include "common/util.hpp"
#include "common/log.hpp"

#include "vk-wsi.h"

#include "common/event_loop.hpp"

// -----------------------------------------------------------------------------

struct Display;

void display_run(int argc, char* argv[]);
void display_terminate(Display*);

// -----------------------------------------------------------------------------

struct Renderer;

// -----------------------------------------------------------------------------

struct Backend;

void backend_init(Display*);
void backend_destroy(Backend*);

std::span<const char* const> backend_get_required_instance_extensions(Backend*);

// -----------------------------------------------------------------------------

struct Output
{
    Display* display;

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

struct Keyboard
{
    Display* display;

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
    Display* display;
};

void pointer_added(   Pointer*);
void pointer_button(  Pointer*, u32 button, bool pressed);
void pointer_absolute(Pointer*, Output*, vec2 pos);
void pointer_relative(Pointer*, vec2 rel);
void pointer_axis(    Pointer*, vec2 rel);

// -----------------------------------------------------------------------------

struct Display
{
    Backend*   backend;
    Renderer*  renderer;
    EventLoop* event_loop;
};
