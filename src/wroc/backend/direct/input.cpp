#include "backend.hpp"

wroc_device* open_device(wroc_direct_backend* backend, const char* path)
{
    int fd = -1;
    auto dev_id = libseat_open_device(backend->seat, path, &fd);
    if (fd < 0) {
        log_error("Failed to open device");
        return nullptr;
    }

    auto device = wrei_adopt_ref(wrei_get_registry(backend)->create<wroc_device>());
    device->dev_id = dev_id;
    device->fd = fd;

    backend->devices.emplace_back(device);

    return device.get();
}

void close_device(wroc_direct_backend* backend, wroc_device* device)
{
    libseat_close_device(backend->seat, device->dev_id);
    close(device->fd);
    std::erase_if(backend->devices, [&](const auto& d) { return d.get() == device; });
}

// -----------------------------------------------------------------------------

static
void seat_enable(libseat* seat, void* data)
{
    log_debug("SEAT ENABLE");
}

static
void seat_disable(libseat* seat, void* data)
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
    log_debug("OPEN RESTRICTED");
    log_debug("  path: {}", path ?: "nullptr");
    auto* backend = static_cast<wroc_direct_backend*>(data);

    auto device = open_device(backend, path);
    if (!device) {
        log_error("Failed to open");
        return -1;
    }

    log_info("  device.id: {}", device->dev_id);
    log_info("  device.fd: {}", device->fd);

    return device->fd;
}

static
void close_restricted(int fd, void* data)
{
    log_debug("CLOSE RESTRICTED: {}", fd);
    auto* backend = static_cast<wroc_direct_backend*>(data);

    auto iter = std::ranges::find(backend->devices, fd, [&](const auto& d) { return d->fd; });
    if (iter != backend->devices.end()) {
        log_debug("Closing device: (id = {})", iter->get()->dev_id);
        close_device(backend, iter->get());
    } else {
        log_error("Could not find open device for fd: {}", fd);
    }
}

static constexpr libinput_interface wroc_libinput_interface {
    .open_restricted = open_restricted,
    .close_restricted = close_restricted,
};

static
int handle_libinput_readable(int fd, u32 mask, void* data)
{
    auto* backend = static_cast<wroc_direct_backend*>(data);
    wrei_unix_check_ne(libinput_dispatch(backend->libinput));

    libinput_event* event;
    while ((event = libinput_get_event(backend->libinput))) {
        wroc_backend_handle_libinput_event(backend, event);
        libinput_event_destroy(event);
    }

    return 0;
}

// -----------------------------------------------------------------------------

static
void log_libinput(libinput* libinput, libinput_log_priority priority, const char* fmt, va_list args) {
    wrei_log_level level;
    switch (priority) {
        break;case LIBINPUT_LOG_PRIORITY_ERROR: level = wrei_log_level::error;
        break;case LIBINPUT_LOG_PRIORITY_INFO: level = wrei_log_level::info;
        break;default: level = wrei_log_level::debug;
    }

	static char wlr_fmt[4096] = {};
	vsnprintf(wlr_fmt, sizeof(wlr_fmt) - 1, fmt, args);
    wrei_log(level, std::format("[libinput] {}", wlr_fmt));
}

// -----------------------------------------------------------------------------

void wroc_backend_init_libinput(wroc_direct_backend* backend)
{
    // udev

    log_debug("INIT BACKEND LIBINPUT, backend: {}", (void*)backend);

    backend->udev = udev_new();
    if (!backend->udev) {
        log_error("Input backend init failed: Failed to create udev context");
        return;
    }

    // libseat

    backend->seat = libseat_open_seat(&wroc_seat_listener, nullptr);
    if (!backend->seat) {
        log_error("Failed to open seat");
        std::terminate();
    }

    backend->seat_name = libseat_seat_name(backend->seat);
    log_info("seat name: {}", backend->seat_name);

    int seat_fd = libseat_get_fd(backend->seat);
    assert(seat_fd >= 0);

    backend->libseat_event_source = wl_event_loop_add_fd(
        wl_display_get_event_loop(backend->server->display), seat_fd, WL_EVENT_READABLE, handle_libseat_readable, backend);

    // libinput

    backend->libinput = libinput_udev_create_context(&wroc_libinput_interface, backend, backend->udev);
    if (!backend->libinput) {
        log_error("Failed to create libinput context");
        std::terminate();
    }

	libinput_log_set_handler(backend->libinput, log_libinput);
	libinput_log_set_priority(backend->libinput, LIBINPUT_LOG_PRIORITY_DEBUG);

    auto res = wrei_unix_check_n1(libinput_udev_assign_seat(backend->libinput, backend->seat_name));
    if (res < 0) {
        log_error("libinput failed to acquire seat");
        std::terminate();
    }

    int libinput_fd = libinput_get_fd(backend->libinput);
    log_debug("LIBINPUT FD = {}", libinput_fd);

    handle_libinput_readable(libinput_fd, WL_EVENT_READABLE, backend);
    if (backend->input_devices.empty()) {
        log_error("LIBINPUT initialization failed, no keyboard or mouse detected");
        std::terminate();
    }

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
