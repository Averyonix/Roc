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

struct wrio_output : wrei_object
{
    virtual void commit() = 0;
};

struct wrio_input_device : wrei_object
{
};
