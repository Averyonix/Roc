#pragma once

#include "wrio.hpp"

#define WRIO_BACKEND(Name) \
    struct Name; \
    WREI_OBJECT_EXPLICIT_DECLARE(Name); \
    void Name##_init(wrio_context*)

WRIO_BACKEND(wrio_udev);
WRIO_BACKEND(wrio_session);
WRIO_BACKEND(wrio_libinput);
WRIO_BACKEND(wrio_evdev);
WRIO_BACKEND(wrio_drm);
WRIO_BACKEND(wrio_wayland);

void wrio_wayland_create_output(wrio_context*);

struct wrio_context : wrei_object
{
    ref<wrei_event_loop> event_loop;

    ref<wrio_udev>     udev;
    ref<wrio_session>  session;
    ref<wrio_libinput> libinput; // input_device
    ref<wrio_evdev>    evdev;    // input_device
    ref<wrio_drm>      drm;      // output
    ref<wrio_wayland>  wayland;  // output | input_device

    ref<wren_context> wren;

    std::vector<ref<wrio_input_device>> input_devices;
    std::vector<ref<wrio_output>>       outputs;

    ref<wrio_layer_stack> scene;
};

// -----------------------------------------------------------------------------

enum class wrio_output_commit_flag : u32
{
    vsync = 1 << 0,
};

struct wrio_swapchain
{
    struct release_slot
    {
        ref<wren_semaphore> semaphore;
        ref<wren_image> image;
        u64 release_point;
    };

    std::vector<ref<wren_image>> free_images;
    std::vector<release_slot> release_slots;

    u32 max_images = 2;
    u32 images_in_flight;
};

struct wrio_output : wrei_object
{
    wrio_context* ctx;

    vec2u32 size;

    wrio_swapchain swapchain;

    // True if commit will accept a new frame
    bool commit_available = true;
    virtual void commit(wren_image*, wren_syncpoint acquire, wren_syncpoint release, flags<wrio_output_commit_flag>) = 0;
};

void wrio_output_try_render(wrio_output*);

// -----------------------------------------------------------------------------

struct wrio_input_device : wrei_object
{
};
