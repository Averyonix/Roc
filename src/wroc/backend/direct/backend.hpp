#pragma once

#include "wroc/wroc.hpp"

// -----------------------------------------------------------------------------

struct wroc_direct_backend;
struct wroc_input_device;

struct wroc_device : wrei_object
{
    int dev_id;
    int fd;
};

struct wroc_drm_output_state;

struct wroc_drm_output : wroc_output
{
    wroc_drm_output_state* state;

    ~wroc_drm_output();

    virtual wren_image* acquire() final override;
    virtual void present(wren_image*, wren_syncpoint) final override;
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
    int drm_fd = -1;

    struct libseat* seat;
    const char* seat_name;
    struct udev* udev;
    struct libinput* libinput;

    std::vector<ref<wroc_device>> devices;

    std::vector<ref<wroc_input_device>> input_devices;

    std::vector<ref<wroc_drm_output>> outputs;

    ref<wrei_event_source> drm_event_source = {};
    ref<wrei_event_source> libseat_event_source = {};
    ref<wrei_event_source> libinput_event_source = {};

    virtual void init() final override;
    virtual void start() final override;

    virtual int get_preferred_drm_device() final override
    {
        return drm_fd;
    };

    virtual void create_output() final override;
    virtual void destroy_output(wroc_output*) final override;

    ~wroc_direct_backend();
};

inline
wroc_direct_backend* wroc_get_direct_backend()
{
    return static_cast<wroc_direct_backend*>(server->backend.get());
}

wroc_device* wroc_open_restricted(wroc_direct_backend*, const char* name);

void wroc_backend_init_session(wroc_direct_backend*);
void wroc_backend_close_session(wroc_direct_backend*);

void wroc_backend_handle_libinput_event(wroc_direct_backend*, libinput_event*);

void wroc_backend_init_drm(wroc_direct_backend*);
void wroc_backend_start_drm(wroc_direct_backend*);