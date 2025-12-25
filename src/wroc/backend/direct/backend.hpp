#pragma once

#include "wroc/server.hpp"

// -----------------------------------------------------------------------------

struct wroc_direct_backend;
struct wroc_input_device;

struct wroc_device : wrei_object
{
    int dev_id;
    int fd;
};

struct wroc_drm_output : wroc_output
{
    VkDisplayKHR vk_display;

    int eventfd;
    wl_event_source* scanout;
    std::jthread scanout_thread;
    std::atomic<std::chrono::steady_clock::time_point> scanout_time;

    ~wroc_drm_output();
};

struct wroc_libinput_keyboard : wroc_keyboard
{
    wroc_input_device* base;

    virtual void update_leds(libinput_led) final override;
};

struct wroc_libinput_pointer : wroc_pointer
{
    wroc_input_device* base;
};

struct wroc_input_device : wrei_object
{
    wroc_direct_backend* backend;

    libinput_device* handle;

    ref<wroc_libinput_keyboard> keyboard;
    ref<wroc_libinput_pointer> pointer;

    ~wroc_input_device();
};

struct wroc_direct_backend : wroc_backend
{
    wroc_server* server = {};

    struct libseat* seat;
    const char* seat_name;
    struct udev* udev;
    struct libinput* libinput;

    std::vector<ref<wroc_device>> devices;

    std::vector<ref<wroc_drm_output>> outputs;

    std::vector<ref<wroc_input_device>> input_devices;

    wl_event_source* libseat_event_source = {};
    wl_event_source* libinput_event_source = {};

    virtual void create_output() final override;
    virtual void destroy_output(wroc_output*) final override;

    ~wroc_direct_backend();
};

void wroc_direct_backend_init(wroc_server*);

void wroc_backend_init_libinput(wroc_direct_backend*);
void wroc_backend_deinit_libinput(wroc_direct_backend*);

void wroc_backend_handle_libinput_event(wroc_direct_backend*, libinput_event*);

void wroc_backend_init_drm(wroc_direct_backend*);
