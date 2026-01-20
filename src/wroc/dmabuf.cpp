#include "wroc.hpp"

#include "wrei/shm.hpp"
#include "protocol.hpp"

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

    // log_warn("dmabuf {} destroyed, clearing image", (void*)buffer);
    // buffer->image = nullptr;

    log_warn("dmabuf {} destroyed", (void*)buffer);

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

    auto* dma_params = wroc_get_userdata<wroc_dma_buffer_params>(params_resource);
    auto& params = dma_params->params;

    if (dma_params->planes_set == ~0u) {
        wroc_post_error(params_resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE, "Attempted to use buffer params with previous errors");
        return nullptr;
    }
    if (!dma_params->planes_set || std::popcount(dma_params->planes_set + 1) != 1) {
        wroc_post_error(params_resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE, "Incomplete plane set");
        return nullptr;
    }

    params.planes.count = std::popcount(dma_params->planes_set);

    auto* new_resource = wroc_resource_create(client, &wl_buffer_interface, 1, buffer_id);
    auto* buffer = wrei_create_unsafe<wroc_dma_buffer>();
    buffer->resource = new_resource;
    buffer->type = wroc_buffer_type::dma;

    wl_resource_set_implementation(new_resource, &wroc_wl_buffer_impl, buffer, wroc_dmabuf_resource_destroy);

    params.format = format;
    params.extent = {width, height};
    params.flags = zwp_linux_buffer_params_v1_flags(flags);

    buffer->extent = {width, height};
    log_warn("Importing DMA-BUF, size = {}, format = {}, mod = {}",
        wrei_to_string(buffer->extent), format->name, wren_drm_modifier_get_name(params.modifier));
    buffer->dmabuf_image = wren_image_import_dmabuf(server->renderer->wren.get(), params, wren_image_usage::texture);
    buffer->image = buffer->dmabuf_image;

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

static
void make_ready(wroc_dma_buffer* buffer, weak<wroc_surface> surface, std::chrono::steady_clock::time_point start)
{
    if (server->renderer->noisy_dmabufs) {
        log_warn("DMA-BUF transfer completed in {}", wrei_duration_to_string(std::chrono::steady_clock::now() - start));
    }
    // TODO: Ensure ordering of buffers via surface state queue
    if (surface) {
        buffer->ready(surface.get());
    }
}

static
void dmabuf_wait_syncobj(wroc_buffer_lock* lock, wren_semaphore* timeline, u64 point, weak<wroc_surface> surface, std::chrono::steady_clock::time_point start)
{
    wren_semaphore_wait_value(timeline, point);

    wrei_event_loop_enqueue(server->event_loop.get(), [lock, timeline, surface, start] {
        auto* dmabuf = static_cast<wroc_dma_buffer*>(lock->buffer.get());
        make_ready(dmabuf, surface, start);
        wrei_remove_ref(lock);
        wrei_remove_ref(timeline);
    });
}

static
void dmabuf_wait_implicit(wroc_buffer_lock* lock, weak<wroc_surface> surface, std::chrono::steady_clock::time_point start)
{
    auto* dmabuf_image = static_cast<wren_image_dmabuf*>(lock->buffer->image.get());

    for (auto& plane : dmabuf_image->dma_params.planes) {
        wrei_unix_check_n1(poll(wrei_ptr_to(pollfd {
            .fd = plane.fd,
            .events = POLLIN,
        }), 1, -1));
    }

    wrei_event_loop_enqueue(server->event_loop.get(), [lock, surface, start] {
        auto* dmabuf = static_cast<wroc_dma_buffer*>(lock->buffer.get());
        make_ready(dmabuf, surface, start);
        wrei_remove_ref(lock);
    });
}

void wroc_dma_buffer::on_commit(wroc_surface* surface)
{
    if (!server->renderer->copy_dmabufs) {
        release();
        return;
    }

    auto start = std::chrono::steady_clock::now();

    auto* syncobj_surface = wroc_surface_get_addon<wroc_syncobj_surface>(surface);
    bool use_syncobj = true;

    if (!syncobj_surface) {
        use_syncobj = false;
    } else if (!syncobj_surface->acquire_timeline && !syncobj_surface->release_timeline) {
        log_error("Surface has syncobj_surface attached, but did not submit any timeline semaphores");
        use_syncobj = false;
    } else if (!syncobj_surface->acquire_timeline) {
        wroc_post_error(resource, WP_LINUX_DRM_SYNCOBJ_SURFACE_V1_ERROR_NO_ACQUIRE_POINT, "Missing acquire point");
        use_syncobj = false;
    } else if (!syncobj_surface->release_timeline) {
        wroc_post_error(resource, WP_LINUX_DRM_SYNCOBJ_SURFACE_V1_ERROR_NO_RELEASE_POINT, "Missing release point");
        use_syncobj = false;
    } else if (syncobj_surface->acquire_timeline.get() == syncobj_surface->release_timeline.get()
            && syncobj_surface->acquire_point >= syncobj_surface->release_point) {
        wroc_post_error(resource, WP_LINUX_DRM_SYNCOBJ_SURFACE_V1_ERROR_CONFLICTING_POINTS, "Acquire and release use the same syncobj, but acquire point is not less than release point");
        use_syncobj = false;
    }

    if (use_syncobj) {

        // Consume the committed timelines
        auto acquire_timeline = std::exchange(syncobj_surface->acquire_timeline, nullptr);
        auto acquire_point = syncobj_surface->acquire_point;

        release_timeline = std::exchange(syncobj_surface->release_timeline, nullptr);
        release_point = syncobj_surface->release_point;

        // TODO: Thread pool?

        std::thread {
            dmabuf_wait_syncobj,
            wrei_add_ref(lock().get()),
            wrei_add_ref(acquire_timeline.get()),
            acquire_point,
            weak(surface),
            start
        }.detach();
    } else {
        std::thread {
            dmabuf_wait_implicit,
            wrei_add_ref(lock().get()),
            weak(surface),
            start
        }.detach();
    }

#if 0
    // TODO: We should still include these barriers for correctness, even if the driver works
    //       fine without them (or at least seems too).

    wren->vk.CmdPipelineBarrier2(commands->buffer, wrei_ptr_to(VkDependencyInfo {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = wrei_ptr_to(VkImageMemoryBarrier2 {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            .srcAccessMask = 0,
            // .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
            // .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT,
            .dstQueueFamilyIndex = queue->family,
            .image = dmabuf_image->image,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        }),
    }));

    wren->vk.CmdPipelineBarrier2(commands->buffer, wrei_ptr_to(VkDependencyInfo {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = wrei_ptr_to(VkImageMemoryBarrier2 {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
            .dstAccessMask = 0,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = queue->family,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT,
            .image = dmabuf_image->image,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        }),
    }));
#endif
}

void wroc_dma_buffer::on_unlock()
{
    release();
    if (release_timeline) {
        wren_semaphore_signal_value(release_timeline.get(), release_point);

        release_timeline = nullptr;
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
