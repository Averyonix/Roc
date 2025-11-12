#include "protocol.hpp"

#include "compositor/server.hpp"
#include "renderer/renderer.hpp"

#define INTERFACE_STUB [](auto...){}

// -----------------------------------------------------------------------------

const struct wl_compositor_interface impl_wl_compositor = {
    .create_region = [](wl_client* client, wl_resource* resource, u32 id) {
        auto* compositor = get_userdata<Compositor>(resource);
        auto* new_resource = wl_resource_create(client, &wl_region_interface, wl_resource_get_version(resource), id);
        debug_track_resource(new_resource);
        auto* region = new Region {};
        region->server = compositor->server;
        region->wl_region = new_resource;
        pixman_region32_init(&region->region);
        wl_resource_set_implementation(new_resource, &impl_wl_region, region, SIMPLE_RESOURCE_UNREF(Region, wl_region));
    },
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

const struct wl_region_interface impl_wl_region = {
    .add = [](wl_client* client, wl_resource* resource, i32 x, i32 y, i32 width, i32 height) {
        auto* region = get_userdata<Region>(resource);
        pixman_region32_union_rect(&region->region, &region->region, x, y, width, height);
    },
    .destroy = [](wl_client* client, wl_resource* resource) {
        wl_resource_destroy(resource);
    },
    .subtract = [](wl_client* client, wl_resource* resource, i32 x, i32 y, i32 width, i32 height) {
        auto* region = get_userdata<Region>(resource);

        pixman_region32_union_rect(&region->region, &region->region, x, y, width, height);

        pixman_region32_t rect;
        pixman_region32_init_rect(&rect, x, y, width, height);
        pixman_region32_subtract(&region->region, &region->region, &rect);
        pixman_region32_fini(&rect);
    },
};

Region::~Region()
{
    pixman_region32_fini(&region);
}

// -----------------------------------------------------------------------------

const struct wl_surface_interface impl_wl_surface = {
    .destroy = INTERFACE_STUB,
    .attach = [](wl_client* client, wl_resource* resource, wl_resource* wl_buffer, i32 x, i32 y) {
        auto* surface = get_userdata<Surface>(resource);
        auto* buffer = get_userdata<ShmBuffer>(wl_buffer);
        log_warn("Attaching buffer, type = {}", magic_enum::enum_name(buffer->type));
        surface->pending.buffer = buffer;
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
                    xdg_toplevel_send_wm_capabilities(surface->xdg_toplevel, ptr_to(to_array<const xdg_toplevel_wm_capabilities>({
                        XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN,
                        XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE,
                    })));
                }
            }
            if (surface->xdg_surface) {
                xdg_surface_send_configure(surface->xdg_surface, wl_display_next_serial(surface->server->display));
            }
        }

        if (surface->pending.buffer) {
            auto* vk = surface->server->renderer->vk;
            if (surface->current.image.image) {
                vk_image_destroy(vk, surface->current.image);
            }

            if (surface->pending.buffer->wl_buffer) {
                auto* buffer = surface->pending.buffer.get();
                if (buffer->type == BufferType::shm) {
                    auto* shm_buffer = static_cast<ShmBuffer*>(buffer);
                    surface->current.image = vk_image_create(vk, {u32(shm_buffer->width), u32(shm_buffer->height)}, static_cast<char*>(shm_buffer->pool->data) + shm_buffer->offset);
                    wl_buffer_send_release(surface->pending.buffer->wl_buffer);
                } else {
                    auto* dma_buffer = static_cast<DmaBuffer*>(buffer);
                    surface->current.image = dma_buffer->image;
                    dma_buffer->image = {};
                    log_warn("User committed dmabuf, size = ({}, {})!", surface->current.image.extent.width, surface->current.image.extent.height);
                    // surface->current.image = vk_image_import_dmabuf(vk, dma_buffer->params);
                    // close(dma_buffer->params.planes.front().fd);
                }

                // wl_buffer_send_release(surface->pending.buffer->wl_buffer);
            } else {
                log_warn("pending wl_buffer was destroyed, surface contents has been cleared");
            }

            surface->pending.buffer = nullptr;
        }

        if (auto& pending = surface->pending.geometry) {
            if (!pending->extent.x || !pending->extent.y) {
                log_warn("Zero size invalid geometry committed, treating as if geometry never set!");
            } else {
                surface->current.geometry = *pending;
            }
            surface->pending.geometry = std::nullopt;
        }

        if (surface->current.geometry) {
            log_debug("Geometry: (({}, {}), ({}, {}))",
                surface->current.geometry->origin.x, surface->current.geometry->origin.y,
                surface->current.geometry->extent.x, surface->current.geometry->extent.y);
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

    if (current.image.image) {
        vk_image_destroy(server->renderer->vk, current.image);
    }
}

// -----------------------------------------------------------------------------

const struct xdg_wm_base_interface impl_xdg_wm_base = {
    .create_positioner = INTERFACE_STUB,
    .destroy = INTERFACE_STUB,
    .get_xdg_surface = [](wl_client* client, wl_resource* resource, u32 id, wl_resource* wl_surface) {
        auto* new_resource = wl_resource_create(client, &xdg_surface_interface, wl_resource_get_version(resource), id);
        debug_track_resource(new_resource);
        auto* surface = ref(get_userdata<Surface>(wl_surface));
        surface->xdg_surface = new_resource;
        wl_resource_set_implementation(new_resource, &impl_xdg_surface, surface, SIMPLE_RESOURCE_UNREF(Surface, xdg_surface));
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
        auto* surface = ref(get_userdata<Surface>(resource));
        auto* new_resource = wl_resource_create(client, &xdg_toplevel_interface, wl_resource_get_version(resource), id);
        debug_track_resource(new_resource);
        surface->xdg_toplevel = new_resource;
        wl_resource_set_implementation(new_resource, &impl_xdg_toplevel, surface, SIMPLE_RESOURCE_UNREF(Surface, xdg_toplevel));
    },
    .get_popup = INTERFACE_STUB,
    .set_window_geometry = [](wl_client* client, wl_resource* resource, i32 x, i32 y, i32 width, i32 height) {
        auto* surface = get_userdata<Surface>(resource);
        surface->pending.geometry = {{x, y}, {width, height}};
    },
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
    .release = [](wl_client* client, wl_resource* resource) {
        wl_resource_destroy(resource);
    },
};

const wl_global_bind_func_t bind_wl_shm = [](wl_client* client, void* data, u32 version, u32 id) {
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

const wl_global_bind_func_t bind_wl_seat = [](wl_client* client, void* data, u32 version, u32 id) {
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

// -----------------------------------------------------------------------------

const struct zwp_linux_dmabuf_v1_interface impl_zwp_linux_dmabuf_v1 = {
    .create_params = [](wl_client* client, wl_resource* resource, u32 params_id) {
        auto* new_resource = wl_resource_create(client, &zwp_linux_buffer_params_v1_interface, wl_resource_get_version(resource), params_id);
        auto* params = new ZwpBufferParams {};
        params->server = get_userdata<Server>(resource);
        params->zwp_linux_buffer_params_v1 = new_resource;
        wl_resource_set_implementation(new_resource, &impl_zwp_linux_buffer_params_v1, params, SIMPLE_RESOURCE_UNREF(ZwpBufferParams, zwp_linux_buffer_params_v1));
    },
    .destroy = [](wl_client* client, wl_resource* resource) {
        wl_resource_destroy(resource);
    },
    .get_default_feedback = [](wl_client* client, wl_resource* resource, u32 id) {
        auto* new_resource = wl_resource_create(client, &zwp_linux_dmabuf_feedback_v1_interface, wl_resource_get_version(resource), id);
        wl_resource_set_implementation(new_resource, &impl_zwp_linux_dmabuf_feedback_v1, nullptr, nullptr);
    },
    .get_surface_feedback = [](wl_client* client, wl_resource* resource, u32 id, wl_resource* surface) {
        auto* new_resource = wl_resource_create(client, &zwp_linux_dmabuf_feedback_v1_interface, wl_resource_get_version(resource), id);
        wl_resource_set_implementation(new_resource, &impl_zwp_linux_dmabuf_feedback_v1, nullptr, nullptr);
    },
};

const struct zwp_linux_buffer_params_v1_interface impl_zwp_linux_buffer_params_v1 = {
    .add = [](wl_client* client, wl_resource* resource, int fd, u32 plane_idx, u32 offset, u32 stride, u32 modifier_hi, u32 modifier_lo) {
        auto* params = get_userdata<ZwpBufferParams>(resource);
        if (!params->params.planes.empty()) {
            log_error("Multiple plane formats not currently supported");
        }
        params->params.planes.emplace_back(DmaPlane{
            .fd = fd,
            .plane_idx = plane_idx,
            .offset = offset,
            .stride = stride,
            .drm_modifier = u64(modifier_hi) << 32 | modifier_lo,
        });
    },
    .create = INTERFACE_STUB,
    .create_immed = [](wl_client* client, wl_resource* resource, u32 buffer_id, i32 width, i32 height, u32 format, u32 flags) {
        auto* params = get_userdata<ZwpBufferParams>(resource);
        auto* new_resource = wl_resource_create(client, &wl_buffer_interface, 1, buffer_id);
        auto* buffer = new DmaBuffer {};
        buffer->server = params->server;
        buffer->wl_buffer = new_resource;
        buffer->type = BufferType::dma;
        buffer->params = std::move(params->params);
        wl_resource_set_implementation(new_resource, &impl_wl_buffer_for_dmabuf, buffer, SIMPLE_RESOURCE_UNREF(DmaBuffer, wl_buffer));

        buffer->params.format = vk_find_format_from_drm(format).value();
        buffer->params.extent = { u32(width), u32(height) };
        buffer->params.flags = zwp_linux_buffer_params_v1_flags(flags);

        buffer->image = vk_image_import_dmabuf(buffer->server->renderer->vk, buffer->params);
    },
    .destroy = [](wl_client* client, wl_resource* resource) {
        wl_resource_destroy(resource);
    },
};

const struct zwp_linux_dmabuf_feedback_v1_interface impl_zwp_linux_dmabuf_feedback_v1 = {
    .destroy = INTERFACE_STUB,
};

const struct wl_buffer_interface impl_wl_buffer_for_dmabuf = {
    .destroy = [](wl_client* client, wl_resource* resource) {
        wl_resource_destroy(resource);
    },
};

const wl_global_bind_func_t bind_zwp_linux_dmabuf_v1 = [](wl_client* client, void* data, u32 version, u32 id) {
    auto* new_resource = wl_resource_create(client, &zwp_linux_dmabuf_v1_interface, version, id);
    wl_resource_set_implementation(new_resource, &impl_zwp_linux_dmabuf_v1, data, nullptr);

    auto send_modifier = [&](u32 format, u64 modifier) {
        zwp_linux_dmabuf_v1_send_modifier(new_resource, format, modifier >> 32, modifier & 0xFFFF'FFFF);
    };

    for (auto& format : {
        DRM_FORMAT_XRGB8888,
        DRM_FORMAT_ARGB8888,
    }) {
        zwp_linux_dmabuf_v1_send_format(new_resource, format);
        // send_modifier(format, DRM_FORMAT_MOD_INVALID);
        send_modifier(format, DRM_FORMAT_MOD_LINEAR);
    }
};
