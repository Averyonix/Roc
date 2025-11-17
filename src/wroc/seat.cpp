#include "server.hpp"
#include "util.hpp"

static
void wroc_wl_seat_get_keyboard(wl_client* client, wl_resource* resource, u32 id)
{
    auto* seat = wroc_get_userdata<wroc_seat>(resource);
    auto* new_resource = wl_resource_create(client, &wl_keyboard_interface, wl_resource_get_version(resource), id);
    wroc_debug_track_resource(new_resource);
    seat->keyboard->wl_keyboards.emplace_back(new_resource);
    wroc_resource_set_implementation(new_resource, &wroc_wl_keyboard_impl, seat->keyboard);

    wl_keyboard_send_keymap(new_resource, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, seat->keyboard->keymap_fd, seat->keyboard->keymap_size);
}

static
void wroc_wl_seat_get_pointer(wl_client* client, wl_resource* resource, u32 id)
{
    auto* seat = wroc_get_userdata<wroc_seat>(resource);
    auto* new_resource = wl_resource_create(client, &wl_pointer_interface, wl_resource_get_version(resource), id);
    wroc_debug_track_resource(new_resource);
    seat->pointer->wl_pointers.emplace_back(new_resource);
    wroc_resource_set_implementation(new_resource, &wroc_wl_pointer_impl, seat->pointer);
}

const struct wl_seat_interface wroc_wl_seat_impl = {
    .get_keyboard = wroc_wl_seat_get_keyboard,
    .get_pointer  = wroc_wl_seat_get_pointer,
    .get_touch    = WROC_STUB,
    .release      = wroc_simple_resource_destroy_callback,
};

const struct wl_keyboard_interface wroc_wl_keyboard_impl = {
    .release = wroc_simple_resource_destroy_callback,
};

static
void wroc_wl_pointer_set_cursor(wl_client* client, wl_resource* resource, u32 serial, wl_resource* wl_surface, i32 x, i32 y)
{
    // TODO: Check serial

    auto* pointer = wroc_get_userdata<wroc_pointer>(resource);
    if (!pointer->focused_surface) {
        log_warn("set_cursor failed, no focused surface");
        return;
    }
    if (wl_resource_get_client(pointer->focused_surface->wl_surface) != client) {
        log_warn("set_cursor failed, client does not own currently focused surface");
        return;
    }

    if (wl_surface) {
        pointer->server->cursor_surface = wrei_weak_from(wroc_get_userdata<wroc_surface>(wl_surface));
        pointer->server->cursor_hotspot = {x, y};
        log_warn("Setting cursor surface to: {} ({}, {})", (void*)pointer->server->cursor_surface.get(), x, y);
    } else {
        pointer->server->cursor_surface = nullptr;
        log_warn("Hiding cursor surface");
    }
}

const struct wl_pointer_interface wroc_wl_pointer_impl = {
    .release    = wroc_simple_resource_destroy_callback,
    .set_cursor = wroc_wl_pointer_set_cursor,
};

void wroc_wl_seat_bind_global(wl_client* client, void* data, u32 version, u32 id)
{
    auto* seat = static_cast<wroc_seat*>(data);
    auto* new_resource = wl_resource_create(client, &wl_seat_interface, version, id);
    wroc_debug_track_resource(new_resource);
    seat->wl_seat.emplace_back(new_resource);
    wroc_resource_set_implementation(new_resource, &wroc_wl_seat_impl, seat);
    if (version >= WL_SEAT_NAME_SINCE_VERSION) {
        wl_seat_send_name(new_resource, seat->name.c_str());
    }
    u32 caps = {};
    if (seat->keyboard) caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    if (seat->pointer)  caps |= WL_SEAT_CAPABILITY_POINTER;
    wl_seat_send_capabilities(new_resource, caps);
};
