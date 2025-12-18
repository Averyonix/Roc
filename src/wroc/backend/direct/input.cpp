#include "backend.hpp"

static
void seat_enable(struct libseat* seat, void* data)
{
    log_debug("SEAT ENABLE");
}

static
void seat_disable(struct libseat* seat, void* data)
{
    log_debug("SEAT DISABLE");
}

static constexpr libseat_seat_listener wroc_seat_listener {
    .enable_seat = seat_enable,
    .disable_seat = seat_disable,
};

static
int handle_libseat_readable(int fd, u32 mask, void* data)
{
    log_debug("SEAT DISPATCH");
    auto* backend = static_cast<wroc_direct_backend*>(data);
    libseat_dispatch(backend->seat, 0);
    return 0;
}

// -----------------------------------------------------------------------------

static
int open_restricted(const char* path, int flags, void* data)
{
    log_debug("OPEN RESTRICTED: {}", path);
    auto* backend = static_cast<wroc_direct_backend*>(data);

    // int fd;
    // libseat_open_device(backend->seat, path, &fd);
    // return fd;

    return libseat_open_device(backend->seat, path, nullptr);
}

static
void close_restricted(int fd, void* data)
{
    log_debug("CLOSE RESTRICTED: {}", fd);
    auto* backend = static_cast<wroc_direct_backend*>(data);
    // close(fd);

    libseat_close_device(backend->seat, fd);
}

static constexpr libinput_interface wroc_libinput_interface {
    .open_restricted = open_restricted,
    .close_restricted = close_restricted,
};

static
void handle_libinput_event(libinput_event* event)
{
    auto type = libinput_event_get_type(event);
    log_warn("libinput event: {}", magic_enum::enum_name(type));
}

static
int handle_libinput_readable(int fd, u32 mask, void* data)
{
    auto* backend = static_cast<wroc_direct_backend*>(data);
    wrei_unix_check_ne(libinput_dispatch(backend->libinput));

    libinput_event* event;
    while ((event = libinput_get_event(backend->libinput))) {
        handle_libinput_event(event);
        libinput_event_destroy(event);
    }

    return 0;
}

// -----------------------------------------------------------------------------

void wroc_backend_init_libinput(wroc_direct_backend* backend)
{
    backend->seat = libseat_open_seat(&wroc_seat_listener, nullptr);
    if (!backend->seat) {
        log_error("Failed to open seat");
        return;
    }

    int seat_fd = libseat_get_fd(backend->seat);
    assert(seat_fd >= 0);

    backend->libseat_event_source = wl_event_loop_add_fd(
        wl_display_get_event_loop(backend->server->display), seat_fd, WL_EVENT_READABLE, handle_libseat_readable, backend);

    backend->udev = udev_new();
    backend->libinput = libinput_udev_create_context(&wroc_libinput_interface, backend, backend->udev);
    if (!backend->libinput) {
        log_error("Failed ot create libinput context");
        return;
    }

    auto res = wrei_unix_check_n1(libinput_udev_assign_seat(backend->libinput, "seat-0"));
    if (res == 0) {
        log_debug("LIBINPUT SEAT ACQUIRED SUCCESSFULLY");
    }

    int libinput_fd = libinput_get_fd(backend->libinput);
    log_debug("LIBINPUT FD = {}", libinput_fd);

    handle_libinput_readable(libinput_fd, WL_EVENT_READABLE, backend);

    backend->libinput_event_source = wl_event_loop_add_fd(
        wl_display_get_event_loop(backend->server->display), libinput_fd, WL_EVENT_READABLE, handle_libinput_readable, backend);

    log_debug("LIBINPUT FD event source added");
}

void wroc_backend_deinit_libinput(wroc_direct_backend* backend)
{
    libseat_close_seat(backend->seat);
    libinput_unref(backend->libinput);

    if (backend->libseat_event_source) {
        wl_event_source_remove(backend->libinput_event_source);
    }

    if (backend->libinput_event_source) {
        wl_event_source_remove(backend->libinput_event_source);
    }
}
