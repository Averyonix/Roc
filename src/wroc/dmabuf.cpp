#include "wroc.hpp"

#include "wrei/shm.hpp"

const u32 wroc_zwp_linux_dmabuf_v1_version = 5;

static
void wroc_dmabuf_create_params(wl_client* client, wl_resource* resource, u32 params_id)
{
    auto* new_resource = wroc_resource_create(client, &zwp_linux_buffer_params_v1_interface, wl_resource_get_version(resource), params_id);
    auto* params = wrei_create_unsafe<wroc_dma_buffer_params>();
    params->resource = new_resource;
    wroc_resource_set_implementation_refcounted(new_resource, &wroc_zwp_linux_buffer_params_v1_impl, params);
}

wroc_dma_buffer_params::~wroc_dma_buffer_params()
{
    for (auto& plane : params.planes) {
        close(plane.fd);
    }
}

static
void wroc_dmabuf_send_tranches(wl_resource* feedback_resource);

static
void wroc_dmabuf_get_default_feedback(wl_client* client, wl_resource* resource, u32 id)
{
    auto* new_resource = wroc_resource_create(client, &zwp_linux_dmabuf_feedback_v1_interface, wl_resource_get_version(resource), id);
    wroc_resource_set_implementation(new_resource, &wroc_zwp_linux_dmabuf_feedback_v1_impl, nullptr);

    wroc_dmabuf_send_tranches(new_resource);
}

static
void wroc_dmabuf_get_surface_feedback(wl_client* client, wl_resource* resource, u32 id, wl_resource* surface)
{
    auto* new_resource = wroc_resource_create(client, &zwp_linux_dmabuf_feedback_v1_interface, wl_resource_get_version(resource), id);
    wroc_resource_set_implementation(new_resource, &wroc_zwp_linux_dmabuf_feedback_v1_impl, nullptr);

    // TODO: Surface optimized tranches?

    wroc_dmabuf_send_tranches(new_resource);
}

const struct zwp_linux_dmabuf_v1_interface wroc_zwp_linux_dmabuf_v1_impl = {
    .destroy              = wroc_simple_resource_destroy_callback,
    .create_params        = wroc_dmabuf_create_params,
    .get_default_feedback = wroc_dmabuf_get_default_feedback,
    .get_surface_feedback = wroc_dmabuf_get_surface_feedback,
};

static
void wroc_dmabuf_params_add(wl_client* client, wl_resource* resource, int fd, u32 plane_idx, u32 offset, u32 stride, u32 modifier_hi, u32 modifier_lo)
{
    // TODO: We should enforce a limit on the number of open files a client can have to keep under 1024 for the whole process

    auto* params = wroc_get_userdata<wroc_dma_buffer_params>(resource);

    if (plane_idx >= wren_dma_max_planes) {
        wroc_post_error(resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_IDX, "Invalid plane index");
        return;
    }

    if (params->planes_set & (1 << plane_idx)) {
        wroc_post_error(resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_SET, "Plane already set");
        return;
    }

    auto drm_modifier = u64(modifier_hi) << 32 | modifier_lo;

    if (!params->planes_set) {
        params->params.modifier = drm_modifier;
    } else if (params->params.modifier != drm_modifier) {
        wroc_post_error(resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT, "All planes must use the same DRM modifier");
        params->planes_set = ~0u;
        return;
    }

    params->planes_set |= (1 << plane_idx);

    params->params.planes[plane_idx] = wren_dma_plane{
        .fd = fd,
        .offset = offset,
        .stride = stride,
    };
}

static
void wroc_dmabuf_resource_destroy(wl_resource* resource)
{
    auto* buffer = wroc_get_userdata<wroc_dma_buffer>(resource);

    log_warn("dmabuf {} destroyed, clearing image", (void*)buffer);

    buffer->image = nullptr;

    wrei_remove_ref(buffer);
}

static
wroc_dma_buffer* wroc_dmabuf_create_buffer(wl_client* client, wl_resource* params_resource, u32 buffer_id, i32 width, i32 height, u32 drm_format, u32 flags)
{
    auto format = wren_format_from_drm(drm_format);

    if (!format) {
        wroc_post_error(params_resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT, "Invalid format");
        return nullptr;
    }

    auto* params = wroc_get_userdata<wroc_dma_buffer_params>(params_resource);

    if (params->planes_set == ~0u) {
        wroc_post_error(params_resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE, "Attempted to use buffer params with previous errors");
        return nullptr;
    }
    if (!params->planes_set || std::popcount(params->planes_set + 1) != 1) {
        wroc_post_error(params_resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE, "Incomplete plane set");
        return nullptr;
    }

    params->params.planes.count = std::popcount(params->planes_set);

    auto* new_resource = wroc_resource_create(client, &wl_buffer_interface, 1, buffer_id);
    auto* buffer = wrei_create_unsafe<wroc_dma_buffer>();
    buffer->resource = new_resource;
    buffer->type = wroc_buffer_type::dma;

    wl_resource_set_implementation(new_resource, &wroc_wl_buffer_impl, buffer, wroc_dmabuf_resource_destroy);

    params->params.format = format;
    params->params.extent = {width, height};
    params->params.flags = zwp_linux_buffer_params_v1_flags(flags);

    buffer->extent = {width, height};
    log_warn("Importing DMA-BUF, size = {}, format = {}, mod = {}",
        wrei_to_string(buffer->extent), format->name, wren_drm_modifier_get_name(params->params.modifier));
    buffer->image = wren_image_import_dmabuf(server->renderer->wren.get(), params->params, wren_image_usage::texture);

    return buffer;
}

static
void wroc_dmabuf_params_create_buffer(wl_client* client, wl_resource* params, i32 width, i32 height, u32 format, u32 flags)
{
    auto buffer = wroc_dmabuf_create_buffer(client, params, 0, width, height, format, flags);
    if (buffer) {
        wroc_send(zwp_linux_buffer_params_v1_send_created, params, buffer->resource);
    } else {
        wroc_send(zwp_linux_buffer_params_v1_send_failed, params);
    }
}

static
void wroc_dmabuf_params_create_buffer_immed(wl_client* client, wl_resource* params, u32 buffer_id, i32 width, i32 height, u32 format, u32 flags) {
    wroc_dmabuf_create_buffer(client, params, buffer_id, width, height, format, flags);
}

const struct zwp_linux_buffer_params_v1_interface wroc_zwp_linux_buffer_params_v1_impl = {
    .destroy      = wroc_simple_resource_destroy_callback,
    .add          = wroc_dmabuf_params_add,
    .create       = wroc_dmabuf_params_create_buffer,
    .create_immed = wroc_dmabuf_params_create_buffer_immed,
};

const struct zwp_linux_dmabuf_feedback_v1_interface wroc_zwp_linux_dmabuf_feedback_v1_impl = {
    .destroy = wroc_simple_resource_destroy_callback,
};

void wroc_dma_buffer::on_commit()
{
    needs_wait = true;
}

void wroc_dma_buffer::on_unlock()
{
    release();
}

void wroc_dma_buffer::on_read(wren_commands* commands, std::vector<ref<wren_semaphore>>& waits)
{
    if (needs_wait) {
        needs_wait = false;

        auto* dma_image = static_cast<wren_image_dmabuf*>(image.get());
        for (auto& plane : dma_image->dma_params.planes) {
            dma_buf_export_sync_file data {
                .flags = DMA_BUF_SYNC_READ,
                .fd = -1,
            };
            wrei_unix_check_n1(drmIoctl(plane.fd, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &data));
            if (data.fd != -1) {
                auto sema = wren_semaphore_import_syncfile(commands->ctx, data.fd);
                if (sema) waits.emplace_back(sema);
            } else {
                log_error("Failed to export syncfile from DMA-BUF");
            }
        }
    }
}

void wroc_renderer_init_buffer_feedback(wroc_renderer* renderer)
{
    auto wren = renderer->wren;

    struct tranche_entry
    {
        u32 format;
        u32 padding;
        u64 modifier;
    };

    auto& feedback = renderer->buffer_feedback;

    // Enumerate formats

    std::vector<tranche_entry> entries;
    // entries.emplace_back(DRM_FORMAT_XRGB8888, 0, DRM_FORMAT_MOD_LINEAR);
    // entries.emplace_back(DRM_FORMAT_ARGB8888, 0, DRM_FORMAT_MOD_LINEAR);
    for (auto[format, modifiers] : wren->dmabuf_texture_formats) {
        for (auto modifier : modifiers) {
            entries.emplace_back(tranche_entry {
                .format = format->drm,
                .modifier = modifier,
            });
        }
    }

    // Copy to shared memory

    usz size = entries.size() * sizeof(tranche_entry);
    int rw, ro;
    if (!wrei_allocate_shm_file_pair(size, &rw, &ro)) {
        log_error("Failed to allocate shared memory pair");
        std::terminate();
    }

    void* dst = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, rw, 0);
    close(rw);
    if (dst == MAP_FAILED) {
        log_error("mmap failed");
        close(ro);
        return;
    }

    std::memcpy(dst, entries.data(), size);

    // Generate indices
    // TODO: Move LINEAR modifiers into lower tranche?

    feedback.format_table = ro;
    feedback.format_table_size = size;

    feedback.tranche_formats.resize(entries.size());
    for (u16 i = 0; i < entries.size(); ++i) {
        feedback.tranche_formats[i] = i;
    }
}

static
void wroc_dmabuf_send_tranches(wl_resource* resource)
{
    wl_array dev_id = {
        .size = sizeof(server->renderer->wren->dev_id),
        .alloc = sizeof(server->renderer->wren->dev_id),
        .data = &server->renderer->wren->dev_id,
    };

    auto& feedback = server->renderer->buffer_feedback;

    wroc_send(zwp_linux_dmabuf_feedback_v1_send_main_device, resource, &dev_id);
    wroc_send(zwp_linux_dmabuf_feedback_v1_send_format_table, resource, feedback.format_table, feedback.format_table_size);

    wroc_send(zwp_linux_dmabuf_feedback_v1_send_tranche_target_device, resource, &dev_id);
    wroc_send(zwp_linux_dmabuf_feedback_v1_send_tranche_flags, resource, 0);
    wroc_send(zwp_linux_dmabuf_feedback_v1_send_tranche_formats, resource, wrei_ptr_to(wroc_to_wl_array<u16>(feedback.tranche_formats)));
    wroc_send(zwp_linux_dmabuf_feedback_v1_send_tranche_done, resource);

    wroc_send(zwp_linux_dmabuf_feedback_v1_send_done, resource);
}

void wroc_zwp_linux_dmabuf_v1_bind_global(wl_client* client, void* data, u32 version, u32 id)
{
    auto* new_resource = wroc_resource_create(client, &zwp_linux_dmabuf_v1_interface, version, id);
    wroc_resource_set_implementation(new_resource, &wroc_zwp_linux_dmabuf_v1_impl, nullptr);

    if (version >= ZWP_LINUX_DMABUF_V1_GET_DEFAULT_FEEDBACK_SINCE_VERSION) {
        return;
    }

    // Deprecated method of exposing formats, only use with legacy clients

    auto send_modifier = [&](u32 format, u64 modifier) {
        u32 modifier_hi =  modifier >> 32;
        u32 modifier_lo = modifier & 0xFFFF'FFFF;
        wroc_send(zwp_linux_dmabuf_v1_send_modifier, new_resource, format, modifier_hi, modifier_lo);
    };


    for (auto[format, modifiers] : server->renderer->wren->dmabuf_texture_formats) {
        wroc_send(zwp_linux_dmabuf_v1_send_format, new_resource, format->drm);

        for (auto modifier : modifiers) {
            send_modifier(format->drm, modifier);
        }
    }
};
