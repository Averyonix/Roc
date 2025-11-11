#include "protocol.hpp"

#include "compositor/server.hpp"
#include "renderer/renderer.hpp"

#define INTERFACE_STUB [](auto...){}

// -----------------------------------------------------------------------------

const struct wl_compositor_interface impl_wl_compositor = {
    .create_region = INTERFACE_STUB,
    .create_surface = [](wl_client* client, wl_resource* resource, u32 id) {
        auto* compositor = get_userdata<Compositor>(resource);
        auto* new_resource = wl_resource_create(client, &wl_surface_interface, wl_resource_get_version(resource), id);
        debug_track_resource(new_resource);
        auto* surface = new Surface {};
        surface->server = compositor->server;
        surface->wl_surface = new_resource;
        compositor->server->surfaces.emplace_back(surface);
        wl_resource_set_implementation(new_resource, &impl_wl_surface, surface, SIMPLE_RESOURCE_UNREF(Surface, wl_surface));
    },
};

const wl_global_bind_func_t bind_wl_compositor = [](wl_client* client, void* data, u32 version, u32 id) {
    auto* new_resource = wl_resource_create(client, &wl_compositor_interface, version, id);
    debug_track_resource(new_resource);
    auto* compositor = new Compositor {};
    compositor->server = static_cast<Server*>(data);
    compositor->wl_compositor = new_resource;
    wl_resource_set_implementation(new_resource, &impl_wl_compositor, compositor, SIMPLE_RESOURCE_UNREF(Compositor, wl_compositor));
};

// -----------------------------------------------------------------------------

const struct wl_surface_interface impl_wl_surface = {
    .destroy = INTERFACE_STUB,
    .attach = [](wl_client* client, wl_resource* resource, wl_resource* wl_buffer, i32 x, i32 y) {
        auto* surface = get_userdata<Surface>(resource);
        auto* buffer = get_userdata<ShmBuffer>(wl_buffer);
        surface->pending_buffer = buffer;
    },
    .damage = INTERFACE_STUB,
    .frame = [](wl_client* client, wl_resource* resource, u32 callback) {
        auto* surface = get_userdata<Surface>(resource);
        auto new_resource = wl_resource_create(client, &wl_callback_interface, 1, callback);
        debug_track_resource(new_resource);
        if (surface->frame_callback) {
            wl_resource_destroy(surface->frame_callback);
        }
        log_warn("frame callback {} created", (void*)new_resource);
        surface->frame_callback = new_resource;
        wl_resource_set_implementation(new_resource, nullptr, surface, [](wl_resource* resource) {
            auto* surface = get_userdata<Surface>(resource);
            log_warn("frame callback {} destroyed", (void*)resource);
            if (surface->frame_callback == resource) {
                surface->frame_callback = nullptr;
            }
        });
    },
    .set_opaque_region = INTERFACE_STUB,
    .set_input_region = INTERFACE_STUB,
    .commit = [](wl_client* client, wl_resource* resource) {
        auto* surface = get_userdata<Surface>(resource);
        if (surface->initial_commit) {
            surface->initial_commit = false;
            if (surface->xdg_toplevel) {
                if (wl_resource_get_version(surface->xdg_toplevel) >= XDG_TOPLEVEL_CONFIGURE_BOUNDS_SINCE_VERSION) {
                    xdg_toplevel_send_configure_bounds(surface->xdg_toplevel, 0, 0);
                }
                xdg_toplevel_send_configure(surface->xdg_toplevel, 0, 0, ptr_to(to_array<const xdg_toplevel_state>({ XDG_TOPLEVEL_STATE_ACTIVATED })));
                if (wl_resource_get_version(surface->xdg_toplevel) >= XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION) {
                    xdg_toplevel_send_wm_capabilities(resource, ptr_to(to_array<const xdg_toplevel_wm_capabilities>({
                        XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN,
                        XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE,
                    })));
                }
            }
            if (surface->xdg_surface) {
                xdg_surface_send_configure(surface->xdg_surface, wl_display_next_serial(surface->server->display));
            }
        }

        if (surface->pending_buffer) {
            auto* vk = surface->server->renderer->vk;
            if (surface->current_image.image) {
                vk_image_destroy(vk, surface->current_image);
            }

            if (surface->pending_buffer->wl_buffer) {
                auto* buffer = surface->pending_buffer.get();
                if (buffer->type == BufferType::shm) {
                    auto* shm_buffer = static_cast<ShmBuffer*>(buffer);
                    surface->current_image = vk_image_create(vk, {u32(shm_buffer->width), u32(shm_buffer->height)}, static_cast<char*>(shm_buffer->pool->data) + shm_buffer->offset);
                }

                wl_buffer_send_release(surface->pending_buffer->wl_buffer);
            } else {
                log_warn("pending wl_buffer was destroyed, surface contents has been cleared");
            }

            surface->pending_buffer = nullptr;
        }
    },
    .set_buffer_transform = INTERFACE_STUB,
    .set_buffer_scale = INTERFACE_STUB,
    .damage_buffer = INTERFACE_STUB,
    .offset = INTERFACE_STUB,
};

Surface::~Surface()
{
    std::erase(server->surfaces, this);

    if (current_image.image) {
        vk_image_destroy(server->renderer->vk, current_image);
    }
}

// -----------------------------------------------------------------------------

const struct xdg_wm_base_interface impl_xdg_wm_base = {
    .create_positioner = INTERFACE_STUB,
    .destroy = INTERFACE_STUB,
    .get_xdg_surface = [](wl_client* client, wl_resource* resource, u32 id, wl_resource* wl_surface) {
        auto* new_resource = wl_resource_create(client, &xdg_surface_interface, wl_resource_get_version(resource), id);
        debug_track_resource(new_resource);
        auto* surface = get_userdata<Surface>(wl_surface);
        log_warn("Acquiring xdg_surface for surface: {}", (void*)surface);
        surface->xdg_surface = new_resource;
        wl_resource_set_implementation(new_resource, &impl_xdg_surface, surface, [](wl_resource* xdg_surface) {
            auto* surface = get_userdata<Surface>(xdg_surface);
            log_warn("Destroying xdg_surface for surface: {}", (void*)surface);
            surface->xdg_surface = nullptr;
        });
    },
    .pong = INTERFACE_STUB,
};

const wl_global_bind_func_t bind_xdg_wm_base = [](wl_client* client, void* data, u32 version, u32 id) {
    auto* new_resource = wl_resource_create(client, &xdg_wm_base_interface, version, id);
    debug_track_resource(new_resource);
    auto* wm_base = new XdgWmBase {};
    wm_base->server = static_cast<Server*>(data);
    wm_base->xdg_wm_base = new_resource;
    wl_resource_set_implementation(new_resource, &impl_xdg_wm_base, wm_base, SIMPLE_RESOURCE_UNREF(XdgWmBase, xdg_wm_base));
};

// -----------------------------------------------------------------------------

const struct xdg_surface_interface impl_xdg_surface = {
    .destroy = INTERFACE_STUB,
    .get_toplevel = [](wl_client* client, wl_resource* resource, u32 id) {
        auto* surface = get_userdata<Surface>(resource);
        auto* new_resource = wl_resource_create(client, &xdg_toplevel_interface, wl_resource_get_version(resource), id);
        debug_track_resource(new_resource);
        log_warn("Acquiring role xdg_toplevel for surface: {}", (void*)surface);
        surface->xdg_toplevel = new_resource;
        wl_resource_set_implementation(new_resource, &impl_xdg_toplevel, surface, [](wl_resource* xdg_toplevel) {
            auto* surface = get_userdata<Surface>(xdg_toplevel);
            log_warn("Destroying xdg_toplevel for surface: {}", (void*)surface);
            surface->xdg_toplevel = nullptr;
        });
    },
    .get_popup = INTERFACE_STUB,
    .set_window_geometry = INTERFACE_STUB,
    .ack_configure = INTERFACE_STUB,
};

const struct xdg_toplevel_interface impl_xdg_toplevel = {
    .destroy = INTERFACE_STUB,
    .set_parent = INTERFACE_STUB,
    .set_title = INTERFACE_STUB,
    .set_app_id = INTERFACE_STUB,
    .show_window_menu = INTERFACE_STUB,
    .move = INTERFACE_STUB,
    .resize = INTERFACE_STUB,
    .set_max_size = INTERFACE_STUB,
    .set_min_size = INTERFACE_STUB,
    .set_maximized = INTERFACE_STUB,
    .unset_maximized = INTERFACE_STUB,
    .set_fullscreen = INTERFACE_STUB,
    .unset_fullscreen = INTERFACE_STUB,
};

// -----------------------------------------------------------------------------

const struct wl_shm_interface impl_wl_shm = {
    .create_pool = [](wl_client* client, wl_resource* resource, u32 id, int fd, i32 size) {
        auto* new_resource = wl_resource_create(client, &wl_shm_pool_interface, wl_resource_get_version(resource), id);
        debug_track_resource(new_resource);
        auto* pool = new ShmPool {};
        pool->server = get_userdata<Shm>(resource)->server;
        pool->wl_shm_pool = new_resource;
        pool->fd = fd;
        pool->size = size;
        wl_resource_set_implementation(new_resource, &impl_wl_shm_pool, pool, SIMPLE_RESOURCE_UNREF(ShmPool, wl_shm_pool));
        pool->data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, pool->fd, 0);
        if (pool->data == MAP_FAILED) {
            wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_FD, "mmap failed");
        }
    },
    .release = INTERFACE_STUB,
};

const wl_global_bind_func_t bind_wl_shm = [](wl_client* client, void* data, uint32_t version, uint32_t id) {
    auto* new_resource = wl_resource_create(client, &wl_shm_interface, version, id);
    debug_track_resource(new_resource);
    auto* shm = new Shm {};
    shm->server = static_cast<Server*>(data);
    shm->wl_shm = new_resource;
    wl_resource_set_implementation(new_resource, &impl_wl_shm, shm, SIMPLE_RESOURCE_UNREF(Shm, wl_shm));
    wl_shm_send_format(new_resource, WL_SHM_FORMAT_XRGB8888);
};

const struct wl_shm_pool_interface impl_wl_shm_pool = {
    .create_buffer = [](wl_client* client, wl_resource* resource, u32 id, i32 offset, i32 width, i32 height, i32 stride, u32 format) {
        auto* pool = get_userdata<ShmPool>(resource);

        i32 needed = stride * height + offset;
        if (needed > pool->size) {
            wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_STRIDE, "buffer mapped storage exceeds pool limits");
            return;
        }

        auto* new_resource = wl_resource_create(client, &wl_buffer_interface, wl_resource_get_version(resource), id);
        debug_track_resource(new_resource);
        auto* shm_buffer = new ShmBuffer {};
        shm_buffer->server = get_userdata<ShmPool>(resource)->server;
        shm_buffer->type = BufferType::shm;
        shm_buffer->wl_buffer = new_resource;
        shm_buffer->pool = pool;
        shm_buffer->width = width;
        shm_buffer->height = height;
        shm_buffer->stride = stride;
        shm_buffer->format = wl_shm_format(format);
        wl_resource_set_implementation(new_resource, &impl_wl_buffer_for_shm, shm_buffer, SIMPLE_RESOURCE_UNREF(ShmBuffer, wl_buffer));
    },
    .destroy = [](wl_client* client, wl_resource* resource) {
        wl_resource_destroy(resource);
    },
    .resize = [](wl_client* client, wl_resource* resource, i32 size) {
        auto* pool = get_userdata<ShmPool>(resource);
        munmap(pool->data, pool->size);
        pool->data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, pool->fd, 0);
        pool->size = size;
        if (!pool->data) {
            wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_FD, "mmap failed while resizing pool");
        }
    },
};

ShmPool::~ShmPool()
{
    if (data) munmap(data, size);
}

const struct wl_buffer_interface impl_wl_buffer_for_shm = {
    .destroy = [](wl_client* client, wl_resource* resource) {
        wl_resource_destroy(resource);
    },
};

// -----------------------------------------------------------------------------

const struct wl_seat_interface impl_wl_seat = {
    .get_keyboard = [](wl_client* client, wl_resource* resource, u32 id) {
        auto* seat = get_userdata<Seat>(resource);
        auto* new_resource = wl_resource_create(client, &wl_keyboard_interface, wl_resource_get_version(resource), id);
        debug_track_resource(new_resource);
        seat->keyboard->wl_keyboard.emplace_back(new_resource);
        wl_resource_set_implementation(new_resource, &impl_wl_keyboard, seat->keyboard, [](wl_resource* resource) {
            auto* keyboard = get_userdata<Keyboard>(resource);
            std::erase(keyboard->wl_keyboard, resource);
            if (keyboard->focused == resource) keyboard->focused = nullptr;
        });

        wl_keyboard_send_keymap(new_resource, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, seat->keyboard->keymap_fd, seat->keyboard->keymap_size);
    },
    .get_pointer = INTERFACE_STUB,
    .get_touch = INTERFACE_STUB,
    .release = INTERFACE_STUB,
};

const struct wl_keyboard_interface impl_wl_keyboard = {
    .release = INTERFACE_STUB,
};

const struct wl_pointer_interface impl_wl_pointer = {
    .release = INTERFACE_STUB,
    .set_cursor = INTERFACE_STUB,
};

const wl_global_bind_func_t bind_wl_seat = [](wl_client* client, void* data, uint32_t version, uint32_t id) {
    auto* seat = static_cast<Seat*>(data);
    auto* new_resource = wl_resource_create(client, &wl_seat_interface, version, id);
    debug_track_resource(new_resource);
    seat->wl_seat.emplace_back(new_resource);
    wl_resource_set_implementation(new_resource, &impl_wl_seat, seat, [](wl_resource* resource) {
        auto* seat = get_userdata<Seat>(resource);
        std::erase(seat->wl_seat, resource);
    });
    wl_seat_send_name(new_resource, seat->name.c_str());
    u32 caps = {};
    if (seat->keyboard) caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    if (seat->pointer)  caps |= WL_SEAT_CAPABILITY_POINTER;
    wl_seat_send_capabilities(new_resource, caps);
};
