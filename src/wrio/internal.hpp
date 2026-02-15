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

    std::move_only_function<wrio_event_handler> event_handler;

    std::vector<wrio_input_device*> input_devices;
    std::vector<wrio_output*>       outputs;

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

    ~wrio_output();
};

void wrio_output_try_render(wrio_output*);

void wrio_output_add(   wrio_output*);
void wrio_output_remove(wrio_output*);

// -----------------------------------------------------------------------------

/**
 * Base type for input devices. There are three types of input devices:
 * 1. libinput - Handles keyboard/pointer/tablet/gesture/switch devices with seat access.
 * 2. wayland  - Handles the above devices when running in a nested Wayland session.
 * 3. evdev    - Handles all remaining input devices (gamepad/joystick/etc...) that do not require privileged seat access.
 */
struct wrio_input_device : wrei_object
{
    wrio_context* ctx;
};

void wrio_post_event(wrio_context*, wrio_event*);

void wrio_input_device_add(           wrio_input_device*);
void wrio_input_device_remove(        wrio_input_device*);
void wrio_input_device_leave(         wrio_input_device*);
void wrio_input_device_key_enter(     wrio_input_device*, std::span<const u32> keys);
void wrio_input_device_key_press(     wrio_input_device*, u32 key);
void wrio_input_device_key_release(   wrio_input_device*, u32 key);
void wrio_input_device_pointer_motion(wrio_input_device*, vec2f64 delta);
void wrio_input_device_pointer_axis(  wrio_input_device*, wrio_pointer_axis axis, f64 delta);
