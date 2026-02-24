#pragma once

#include "io.hpp"

#define IO__BACKEND(Name) \
    struct Name; \
    CORE_OBJECT_EXPLICIT_DECLARE(Name); \
    void Name##_init(io_context*)

IO__BACKEND(io_udev);
IO__BACKEND(io_session);
IO__BACKEND(io_libinput);
IO__BACKEND(io_evdev);
IO__BACKEND(io_drm);
IO__BACKEND(io_wayland);

void io_wayland_start(io_context*);

struct io_context : core_object
{
    std::move_only_function<io_event_handler> event_handler;

    core_event_loop* event_loop;
    gpu_context*    gpu;

    std::vector<io_input_device*> input_devices;
    std::vector<io_output*>       outputs;

    ref<io_udev>     udev;
    ref<io_session>  session;
    ref<io_libinput> libinput; // input_device
    ref<io_evdev>    evdev;    // input_device
    ref<io_drm>      drm;      // output
    ref<io_wayland>  wayland;  // output | input_device

    ~io_context();
};

void io_request_shutdown(io_context* ctx, io_shutdown_reason reason);

// -----------------------------------------------------------------------------

enum class io_output_commit_flag : u32
{
    vsync = 1 << 0,
};

struct io_swapchain
{
    struct release_slot
    {
        ref<gpu_semaphore> semaphore;
        ref<gpu_image> image;
        u64 release_point;
    };

    std::vector<ref<gpu_image>> free_images;
    std::vector<release_slot> release_slots;

    u32 max_images = 2;
    u32 images_in_flight;
};

struct io_output : core_object
{
    io_context* ctx;

    bool frame_requested;
    flags<gpu_image_usage> requested_usage;

    vec2u32 size;

    io_swapchain swapchain;

    // True if commit will accept a new frame
    bool commit_available = true;
    virtual void commit(gpu_image*, gpu_syncpoint acquire, gpu_syncpoint release, flags<io_output_commit_flag>) = 0;

    ~io_output();
};

void io_output_try_redraw(io_output*);
void io_output_try_redraw_later(io_output*);
void io_output_post_configure(io_output*);

void io_output_add(   io_output*);
void io_output_remove(io_output*);

// -----------------------------------------------------------------------------

/**
 * Base type for input devices. There are three types of input devices:
 * 1. libinput - Handles keyboard/pointer/tablet/gesture/switch devices with seat access.
 * 2. wayland  - Handles the above devices when running in a nested Wayland session.
 * 3. evdev    - Handles all remaining input devices (gamepad/joystick/etc...) that do not require privileged seat access.
 */
struct io_input_device : core_object
{
    io_context* ctx;

    flags<io_input_device_capability> capabilities;

    std::flat_set<u32> pressed;
};

void io_post_event(io_event*);

void io_input_device_add(           io_input_device*);
void io_input_device_remove(        io_input_device*);
void io_input_device_leave(         io_input_device*);
void io_input_device_key_enter(     io_input_device*, std::span<const u32> keys);
void io_input_device_key_press(     io_input_device*, u32 key);
void io_input_device_key_release(   io_input_device*, u32 key);
void io_input_device_pointer_motion(io_input_device*, vec2f32 delta);
void io_input_device_pointer_scroll(io_input_device*, vec2f32 delta);
