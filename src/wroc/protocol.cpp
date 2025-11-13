#include "protocol.hpp"
#include "wroc/util.hpp"

#include "wroc/server.hpp"

// -----------------------------------------------------------------------------

const struct wl_compositor_interface wroc_wl_compositor_impl = {
    .create_region = [](wl_client* client, wl_resource* resource, u32 id) {
        auto* compositor = wroc_get_userdata<wroc_wl_compositor>(resource);
        auto* new_resource = wl_resource_create(client, &wl_region_interface, wl_resource_get_version(resource), id);
        wroc_debug_track_resource(new_resource);
        auto* region = new wroc_wl_region {};
        region->server = compositor->server;
        region->wl_region = new_resource;
        pixman_region32_init(&region->region);
        wl_resource_set_implementation(new_resource, &wroc_wl_region_impl, region, WROC_SIMPLE_RESOURCE_UNREF(wroc_wl_region, wl_region));
    },
    .create_surface = [](wl_client* client, wl_resource* resource, u32 id) {
        auto* compositor = wroc_get_userdata<wroc_wl_compositor>(resource);
        auto* new_resource = wl_resource_create(client, &wl_surface_interface, wl_resource_get_version(resource), id);
        wroc_debug_track_resource(new_resource);
        auto* surface = new wroc_surface {};
        surface->server = compositor->server;
        surface->wl_surface = new_resource;
        compositor->server->surfaces.emplace_back(surface);
        wl_resource_set_implementation(new_resource, &wroc_wl_surface_impl, surface, WROC_SIMPLE_RESOURCE_UNREF(wroc_surface, wl_surface));
    },
};

const wl_global_bind_func_t wroc_wl_compositor_bind_global = [](wl_client* client, void* data, u32 version, u32 id) {
    auto* new_resource = wl_resource_create(client, &wl_compositor_interface, version, id);
    wroc_debug_track_resource(new_resource);
    auto* compositor = new wroc_wl_compositor {};
    compositor->server = static_cast<wroc_server*>(data);
    compositor->wl_compositor = new_resource;
    wl_resource_set_implementation(new_resource, &wroc_wl_compositor_impl, compositor, WROC_SIMPLE_RESOURCE_UNREF(wroc_wl_compositor, wl_compositor));
};

// -----------------------------------------------------------------------------

const struct wl_region_interface wroc_wl_region_impl = {
    .add = [](wl_client* client, wl_resource* resource, i32 x, i32 y, i32 width, i32 height) {
        auto* region = wroc_get_userdata<wroc_wl_region>(resource);
        pixman_region32_union_rect(&region->region, &region->region, x, y, width, height);
    },
    .destroy = [](wl_client* client, wl_resource* resource) {
        wl_resource_destroy(resource);
    },
    .subtract = [](wl_client* client, wl_resource* resource, i32 x, i32 y, i32 width, i32 height) {
        auto* region = wroc_get_userdata<wroc_wl_region>(resource);

        pixman_region32_union_rect(&region->region, &region->region, x, y, width, height);

        pixman_region32_t rect;
        pixman_region32_init_rect(&rect, x, y, width, height);
        pixman_region32_subtract(&region->region, &region->region, &rect);
        pixman_region32_fini(&rect);
    },
};

wroc_wl_region::~wroc_wl_region()
{
    pixman_region32_fini(&region);
}

// -----------------------------------------------------------------------------

const struct wl_surface_interface wroc_wl_surface_impl = {
    .destroy = WROC_STUB,
    .attach = [](wl_client* client, wl_resource* resource, wl_resource* wl_buffer, i32 x, i32 y) {
        auto* surface = wroc_get_userdata<wroc_surface>(resource);
        if (wl_buffer) {
            auto* buffer = wroc_get_userdata<wroc_shm_buffer>(wl_buffer);
            log_trace("Attaching buffer, type = {}", magic_enum::enum_name(buffer->type));
            surface->pending.buffer = buffer;
        } else {
            surface->pending.buffer = nullptr;
        }
        surface->pending.buffer_was_set = true;
    },
    .damage = WROC_STUB,
    .frame = [](wl_client* client, wl_resource* resource, u32 callback) {
        auto* surface = wroc_get_userdata<wroc_surface>(resource);
        auto new_resource = wl_resource_create(client, &wl_callback_interface, 1, callback);
        wroc_debug_track_resource(new_resource);
        if (surface->frame_callback) {
            wl_resource_destroy(surface->frame_callback);
        }
        // log_warn("frame callback {} created", (void*)new_resource);
        surface->frame_callback = new_resource;
        wl_resource_set_implementation(new_resource, nullptr, surface, [](wl_resource* resource) {
            auto* surface = wroc_get_userdata<wroc_surface>(resource);
            // log_warn("frame callback {} destroyed", (void*)resource);
            if (surface->frame_callback == resource) {
                surface->frame_callback = nullptr;
            }
        });
    },
    .set_opaque_region = WROC_STUB,
    .set_input_region = WROC_STUB,
    .commit = [](wl_client* client, wl_resource* resource) {
        auto* surface = wroc_get_userdata<wroc_surface>(resource);

        // Handle initial commit

        if (surface->initial_commit) {
            surface->initial_commit = false;
            if (surface->xdg_toplevel) {
                if (wl_resource_get_version(surface->xdg_toplevel) >= XDG_TOPLEVEL_CONFIGURE_BOUNDS_SINCE_VERSION) {
                    xdg_toplevel_send_configure_bounds(surface->xdg_toplevel, 0, 0);
                }
                xdg_toplevel_send_configure(surface->xdg_toplevel, 0, 0, wrei_ptr_to(wroc_to_wl_array<const xdg_toplevel_state>({ XDG_TOPLEVEL_STATE_ACTIVATED })));
                if (wl_resource_get_version(surface->xdg_toplevel) >= XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION) {
                    xdg_toplevel_send_wm_capabilities(surface->xdg_toplevel, wrei_ptr_to(wroc_to_wl_array<const xdg_toplevel_wm_capabilities>({
                        XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN,
                        XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE,
                    })));
                }
            }
            if (surface->xdg_surface) {
                xdg_surface_send_configure(surface->xdg_surface, wl_display_next_serial(surface->server->display));
            }
        }

        // Update buffer

        if (surface->pending.buffer_was_set) {
            if (surface->pending.buffer && surface->pending.buffer->locked) {
                log_error("Client is attempting to commit buffer that is already locked!");
            }

            if (surface->current.buffer) {
                surface->current.buffer->unlock();
            }

            if (surface->pending.buffer) {
                if (surface->pending.buffer->wl_buffer) {
                    surface->current.buffer = surface->pending.buffer;
                    surface->current.buffer->on_commit();
                } else {
                    log_warn("Pending buffer was destroyed, surface contents will be cleared");
                    surface->current.buffer = nullptr;
                }
            } else if (surface->current.buffer) {
                log_warn("Null buffer was attached, surface contents will be cleared");
                surface->current.buffer = nullptr;
            }

            surface->pending.buffer = nullptr;
            surface->pending.buffer_was_set = false;
        }

        // Update geometry

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
    .set_buffer_transform = WROC_STUB,
    .set_buffer_scale = WROC_STUB,
    .damage_buffer = WROC_STUB,
    .offset = WROC_STUB,
};

wroc_surface::~wroc_surface()
{
    std::erase(server->surfaces, this);
}

// -----------------------------------------------------------------------------

const struct xdg_wm_base_interface wroc_xdg_wm_base_impl = {
    .create_positioner = WROC_STUB,
    .destroy = WROC_STUB,
    .get_xdg_surface = [](wl_client* client, wl_resource* resource, u32 id, wl_resource* wl_surface) {
        auto* new_resource = wl_resource_create(client, &xdg_surface_interface, wl_resource_get_version(resource), id);
        wroc_debug_track_resource(new_resource);
        auto* surface = wrei_add_ref(wroc_get_userdata<wroc_surface>(wl_surface));
        surface->xdg_surface = new_resource;
        wl_resource_set_implementation(new_resource, &wroc_xdg_surface_impl, surface, WROC_SIMPLE_RESOURCE_UNREF(wroc_surface, xdg_surface));
    },
    .pong = WROC_STUB,
};

const wl_global_bind_func_t wroc_xdg_wm_base_bind_global = [](wl_client* client, void* data, u32 version, u32 id) {
    auto* new_resource = wl_resource_create(client, &xdg_wm_base_interface, version, id);
    wroc_debug_track_resource(new_resource);
    auto* wm_base = new wroc_xdg_wm_base {};
    wm_base->server = static_cast<wroc_server*>(data);
    wm_base->xdg_wm_base = new_resource;
    wl_resource_set_implementation(new_resource, &wroc_xdg_wm_base_impl, wm_base, WROC_SIMPLE_RESOURCE_UNREF(wroc_xdg_wm_base, xdg_wm_base));
};

// -----------------------------------------------------------------------------

const struct xdg_surface_interface wroc_xdg_surface_impl = {
    .destroy = WROC_STUB,
    .get_toplevel = [](wl_client* client, wl_resource* resource, u32 id) {
        auto* surface = wrei_add_ref(wroc_get_userdata<wroc_surface>(resource));
        auto* new_resource = wl_resource_create(client, &xdg_toplevel_interface, wl_resource_get_version(resource), id);
        wroc_debug_track_resource(new_resource);
        surface->xdg_toplevel = new_resource;
        wl_resource_set_implementation(new_resource, &wroc_xdg_toplevel_impl, surface, WROC_SIMPLE_RESOURCE_UNREF(wroc_surface, xdg_toplevel));
    },
    .get_popup = WROC_STUB,
    .set_window_geometry = [](wl_client* client, wl_resource* resource, i32 x, i32 y, i32 width, i32 height) {
        auto* surface = wroc_get_userdata<wroc_surface>(resource);
        surface->pending.geometry = {{x, y}, {width, height}};
    },
    .ack_configure = WROC_STUB,
};

const struct xdg_toplevel_interface wroc_xdg_toplevel_impl = {
    .destroy = WROC_STUB,
    .set_parent = WROC_STUB,
    .set_title = WROC_STUB,
    .set_app_id = WROC_STUB,
    .show_window_menu = WROC_STUB,
    .move = WROC_STUB,
    .resize = WROC_STUB,
    .set_max_size = WROC_STUB,
    .set_min_size = WROC_STUB,
    .set_maximized = WROC_STUB,
    .unset_maximized = WROC_STUB,
    .set_fullscreen = WROC_STUB,
    .unset_fullscreen = WROC_STUB,
};

// -----------------------------------------------------------------------------

const struct wl_shm_interface wroc_wl_shm_impl = {
    .create_pool = [](wl_client* client, wl_resource* resource, u32 id, int fd, i32 size) {
        auto* new_resource = wl_resource_create(client, &wl_shm_pool_interface, wl_resource_get_version(resource), id);
        wroc_debug_track_resource(new_resource);
        auto* pool = new wroc_wl_shm_pool {};
        pool->server = wroc_get_userdata<wroc_wl_shm>(resource)->server;
        pool->wl_shm_pool = new_resource;
        pool->fd = fd;
        pool->size = size;
        wl_resource_set_implementation(new_resource, &wroc_wl_shm_pool_impl, pool, WROC_SIMPLE_RESOURCE_UNREF(wroc_wl_shm_pool, wl_shm_pool));
        pool->data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, pool->fd, 0);
        if (pool->data == MAP_FAILED) {
            wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_FD, "mmap failed");
        }
    },
    .release = [](wl_client* client, wl_resource* resource) {
        wl_resource_destroy(resource);
    },
};

const wl_global_bind_func_t wroc_wl_shm_bind_global = [](wl_client* client, void* data, u32 version, u32 id) {
    auto* new_resource = wl_resource_create(client, &wl_shm_interface, version, id);
    wroc_debug_track_resource(new_resource);
    auto* shm = new wroc_wl_shm {};
    shm->server = static_cast<wroc_server*>(data);
    shm->wl_shm = new_resource;
    wl_resource_set_implementation(new_resource, &wroc_wl_shm_impl, shm, WROC_SIMPLE_RESOURCE_UNREF(wroc_wl_shm, wl_shm));
    wl_shm_send_format(new_resource, WL_SHM_FORMAT_XRGB8888);
};

const struct wl_shm_pool_interface wroc_wl_shm_pool_impl = {
    .create_buffer = [](wl_client* client, wl_resource* resource, u32 id, i32 offset, i32 width, i32 height, i32 stride, u32 format) {
        auto* pool = wroc_get_userdata<wroc_wl_shm_pool>(resource);

        i32 needed = stride * height + offset;
        if (needed > pool->size) {
            wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_STRIDE, "buffer mapped storage exceeds pool limits");
            return;
        }

        auto* new_resource = wl_resource_create(client, &wl_buffer_interface, wl_resource_get_version(resource), id);
        wroc_debug_track_resource(new_resource);
        auto* shm_buffer = new wroc_shm_buffer {};
        shm_buffer->server = wroc_get_userdata<wroc_wl_shm_pool>(resource)->server;
        shm_buffer->type = wroc_wl_buffer_type::shm;
        shm_buffer->wl_buffer = new_resource;
        shm_buffer->pool = pool;
        shm_buffer->width = width;
        shm_buffer->height = height;
        shm_buffer->stride = stride;
        shm_buffer->format = wl_shm_format(format);
        wl_resource_set_implementation(new_resource, &wroc_wl_buffer_for_shm_impl, shm_buffer, WROC_SIMPLE_RESOURCE_UNREF(wroc_shm_buffer, wl_buffer));

        shm_buffer->image = wren_image_create(shm_buffer->server->renderer->wren.get(), {u32(width), u32(height)});

        log_warn("buffer created ({}, {})", width, height);
    },
    .destroy = [](wl_client* client, wl_resource* resource) {
        wl_resource_destroy(resource);
    },
    .resize = [](wl_client* client, wl_resource* resource, i32 size) {
        auto* pool = wroc_get_userdata<wroc_wl_shm_pool>(resource);
        munmap(pool->data, pool->size);
        pool->data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, pool->fd, 0);
        pool->size = size;
        if (!pool->data) {
            wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_FD, "mmap failed while resizing pool");
        }
    },
};

wroc_wl_shm_pool::~wroc_wl_shm_pool()
{
    if (data) munmap(data, size);
}

const struct wl_buffer_interface wroc_wl_buffer_for_shm_impl = {
    .destroy = [](wl_client* client, wl_resource* resource) {
        wl_resource_destroy(resource);
    },
};

void wroc_shm_buffer::on_commit()
{
    lock();
    wren_image_update(image.get(), static_cast<char*>(pool->data) + offset);
    log_debug("buffer updated ({}, {})", width, height);
    unlock();
}

// -----------------------------------------------------------------------------

const struct wl_seat_interface wroc_wl_seat_impl = {
    .get_keyboard = [](wl_client* client, wl_resource* resource, u32 id) {
        auto* seat = wroc_get_userdata<wroc_seat>(resource);
        auto* new_resource = wl_resource_create(client, &wl_keyboard_interface, wl_resource_get_version(resource), id);
        wroc_debug_track_resource(new_resource);
        seat->keyboard->wl_keyboard.emplace_back(new_resource);
        wl_resource_set_implementation(new_resource, &wroc_wl_keyboard_impl, seat->keyboard, [](wl_resource* resource) {
            auto* keyboard = wroc_get_userdata<wroc_keyboard>(resource);
            std::erase(keyboard->wl_keyboard, resource);
            if (keyboard->focused == resource) keyboard->focused = nullptr;
        });

        wl_keyboard_send_keymap(new_resource, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, seat->keyboard->keymap_fd, seat->keyboard->keymap_size);
    },
    .get_pointer = WROC_STUB,
    .get_touch = WROC_STUB,
    .release = WROC_STUB,
};

const struct wl_keyboard_interface wroc_wl_keyboard_impl = {
    .release = WROC_STUB,
};

const struct wl_pointer_interface wroc_wl_pointer_impl = {
    .release = WROC_STUB,
    .set_cursor = WROC_STUB,
};

const wl_global_bind_func_t wroc_wl_seat_bind_global = [](wl_client* client, void* data, u32 version, u32 id) {
    auto* seat = static_cast<wroc_seat*>(data);
    auto* new_resource = wl_resource_create(client, &wl_seat_interface, version, id);
    wroc_debug_track_resource(new_resource);
    seat->wl_seat.emplace_back(new_resource);
    wl_resource_set_implementation(new_resource, &wroc_wl_seat_impl, seat, [](wl_resource* resource) {
        auto* seat = wroc_get_userdata<wroc_seat>(resource);
        std::erase(seat->wl_seat, resource);
    });
    if (version >= WL_SEAT_NAME_SINCE_VERSION) {
        wl_seat_send_name(new_resource, seat->name.c_str());
    }
    u32 caps = {};
    if (seat->keyboard) caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    if (seat->pointer)  caps |= WL_SEAT_CAPABILITY_POINTER;
    wl_seat_send_capabilities(new_resource, caps);
};

// -----------------------------------------------------------------------------

const struct zwp_linux_dmabuf_v1_interface wroc_zwp_linux_dmabuf_v1_impl = {
    .create_params = [](wl_client* client, wl_resource* resource, u32 params_id) {
        auto* new_resource = wl_resource_create(client, &zwp_linux_buffer_params_v1_interface, wl_resource_get_version(resource), params_id);
        auto* params = new wroc_zwp_linux_buffer_params {};
        params->server = wroc_get_userdata<wroc_server>(resource);
        params->zwp_linux_buffer_params_v1 = new_resource;
        wl_resource_set_implementation(new_resource, &wroc_zwp_linux_buffer_params_v1_impl, params, WROC_SIMPLE_RESOURCE_UNREF(wroc_zwp_linux_buffer_params, zwp_linux_buffer_params_v1));
    },
    .destroy = [](wl_client* client, wl_resource* resource) {
        wl_resource_destroy(resource);
    },
    .get_default_feedback = [](wl_client* client, wl_resource* resource, u32 id) {
        auto* new_resource = wl_resource_create(client, &zwp_linux_dmabuf_feedback_v1_interface, wl_resource_get_version(resource), id);
        wl_resource_set_implementation(new_resource, &wroc_zwp_linux_dmabuf_feedback_v1_impl, nullptr, nullptr);
    },
    .get_surface_feedback = [](wl_client* client, wl_resource* resource, u32 id, wl_resource* surface) {
        auto* new_resource = wl_resource_create(client, &zwp_linux_dmabuf_feedback_v1_interface, wl_resource_get_version(resource), id);
        wl_resource_set_implementation(new_resource, &wroc_zwp_linux_dmabuf_feedback_v1_impl, nullptr, nullptr);
    },
};

static
wroc_dma_buffer* wroc_dma_buffer_create(wl_client* client, wl_resource* resource, u32 buffer_id, i32 width, i32 height, u32 format, u32 flags)
{
    auto* params = wroc_get_userdata<wroc_zwp_linux_buffer_params>(resource);
    auto* new_resource = wl_resource_create(client, &wl_buffer_interface, 1, buffer_id);
    auto* buffer = new wroc_dma_buffer {};
    buffer->server = params->server;
    buffer->wl_buffer = new_resource;
    buffer->type = wroc_wl_buffer_type::dma;
    buffer->params = std::move(params->params);
    wl_resource_set_implementation(new_resource, &wroc_wl_buffer_for_dmabuf_impl, buffer, WROC_SIMPLE_RESOURCE_UNREF(wroc_dma_buffer, wl_buffer));

    buffer->params.format = wren_find_format_from_drm(format).value();
    buffer->params.extent = { u32(width), u32(height) };
    buffer->params.flags = zwp_linux_buffer_params_v1_flags(flags);

    buffer->image = wren_image_import_dmabuf(buffer->server->renderer->wren.get(), buffer->params);

    return buffer;
}

const struct zwp_linux_buffer_params_v1_interface wroc_zwp_linux_buffer_params_v1_impl = {
    .add = [](wl_client* client, wl_resource* resource, int fd, u32 plane_idx, u32 offset, u32 stride, u32 modifier_hi, u32 modifier_lo) {
        auto* params = wroc_get_userdata<wroc_zwp_linux_buffer_params>(resource);
        if (!params->params.planes.empty()) {
            log_error("Multiple plane formats not currently supported");
        }
        params->params.planes.emplace_back(wren_dma_plane{
            .fd = fd,
            .plane_idx = plane_idx,
            .offset = offset,
            .stride = stride,
            .drm_modifier = u64(modifier_hi) << 32 | modifier_lo,
        });
    },
    .create = [](wl_client* client, wl_resource* resource, i32 width, i32 height, u32 format, u32 flags) {
        auto buffer = wroc_dma_buffer_create(client, resource, 0, width, height, format, flags);
        if (buffer) {
            zwp_linux_buffer_params_v1_send_created(resource, buffer->wl_buffer);
        } else {
            zwp_linux_buffer_params_v1_send_failed(resource);
        }
    },
    .create_immed = [](wl_client* client, wl_resource* resource, u32 buffer_id, i32 width, i32 height, u32 format, u32 flags) {
        wroc_dma_buffer_create(client, resource, buffer_id, width, height, format, flags);
    },
    .destroy = [](wl_client* client, wl_resource* resource) {
        wl_resource_destroy(resource);
    },
};

const struct zwp_linux_dmabuf_feedback_v1_interface wroc_zwp_linux_dmabuf_feedback_v1_impl = {
    .destroy = WROC_STUB,
};

const struct wl_buffer_interface wroc_wl_buffer_for_dmabuf_impl = {
    .destroy = [](wl_client* client, wl_resource* resource) {
        wl_resource_destroy(resource);
    },
};

void wroc_dma_buffer::on_commit()
{
    // Updated contents will have been written to backing memory
    lock();
}

const wl_global_bind_func_t wroc_zwp_linux_dmabuf_v1_bind_global = [](wl_client* client, void* data, u32 version, u32 id) {
    auto* new_resource = wl_resource_create(client, &zwp_linux_dmabuf_v1_interface, version, id);
    wl_resource_set_implementation(new_resource, &wroc_zwp_linux_dmabuf_v1_impl, data, nullptr);

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
