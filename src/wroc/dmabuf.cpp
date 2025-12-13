#include "server.hpp"

static
void wroc_dmabuf_create_params(wl_client* client, wl_resource* resource, u32 params_id)
{
    auto* new_resource = wl_resource_create(client, &zwp_linux_buffer_params_v1_interface, wl_resource_get_version(resource), params_id);
    auto* server = wroc_get_userdata<wroc_server>(resource);
    auto* params = wrei_get_registry(server)->create<wroc_zwp_linux_buffer_params>();
    params->server = server;
    params->resource = new_resource;
    wroc_resource_set_implementation_refcounted(new_resource, &wroc_zwp_linux_buffer_params_v1_impl, params);
}

wroc_zwp_linux_buffer_params::~wroc_zwp_linux_buffer_params()
{
    for (auto& plane : params.planes) {
        close(plane.fd);
    }
}

static
void wroc_dmabuf_get_default_feedback(wl_client* client, wl_resource* resource, u32 id)
{
    auto* new_resource = wl_resource_create(client, &zwp_linux_dmabuf_feedback_v1_interface, wl_resource_get_version(resource), id);
    wroc_resource_set_implementation(new_resource, &wroc_zwp_linux_dmabuf_feedback_v1_impl, nullptr);
}

static
void wroc_dmabuf_get_surface_feedback(wl_client* client, wl_resource* resource, u32 id, wl_resource* surface)
{
    auto* new_resource = wl_resource_create(client, &zwp_linux_dmabuf_feedback_v1_interface, wl_resource_get_version(resource), id);
    wroc_resource_set_implementation(new_resource, &wroc_zwp_linux_dmabuf_feedback_v1_impl, nullptr);

}

const struct zwp_linux_dmabuf_v1_interface wroc_zwp_linux_dmabuf_v1_impl = {
    .create_params        = wroc_dmabuf_create_params,
    .destroy              = wroc_simple_resource_destroy_callback,
    .get_default_feedback = wroc_dmabuf_get_default_feedback,
    .get_surface_feedback = wroc_dmabuf_get_surface_feedback,
};

static
void wroc_dmabuf_params_add(wl_client* client, wl_resource* resource, int fd, u32 plane_idx, u32 offset, u32 stride, u32 modifier_hi, u32 modifier_lo)
{
    // TODO: We should enforce a limit on the number of open files a client can have to keep under 1024 for the whole process

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
wroc_dma_buffer* wroc_dmabuf_create_buffer(wl_client* client, wl_resource* params_resource, u32 buffer_id, i32 width, i32 height, u32 format, u32 flags)
{
    auto* params = wroc_get_userdata<wroc_zwp_linux_buffer_params>(params_resource);
    auto* new_resource = wl_resource_create(client, &wl_buffer_interface, 1, buffer_id);
    auto* buffer = wrei_get_registry(params)->create<wroc_dma_buffer>();
    buffer->server = params->server;
    buffer->resource = new_resource;
    buffer->type = wroc_wl_buffer_type::dma;

    wl_resource_set_implementation(new_resource, &wroc_wl_buffer_impl, buffer, wroc_dmabuf_resource_destroy);

    params->params.format = wren_find_format_from_drm(format).value();
    params->params.extent = { u32(width), u32(height) };
    params->params.flags = zwp_linux_buffer_params_v1_flags(flags);

    buffer->extent = {width, height};
    buffer->image = wren_image_import_dmabuf(buffer->server->renderer->wren.get(), params->params);

    return buffer;
}

static
void wroc_dmabuf_params_create_buffer(wl_client* client, wl_resource* params, i32 width, i32 height, u32 format, u32 flags)
{
    auto buffer = wroc_dmabuf_create_buffer(client, params, 0, width, height, format, flags);
    if (buffer) {
        zwp_linux_buffer_params_v1_send_created(params, buffer->resource);
    } else {
        zwp_linux_buffer_params_v1_send_failed(params);
    }
}

static
void wroc_dmabuf_params_create_buffer_immed(wl_client* client, wl_resource* params, u32 buffer_id, i32 width, i32 height, u32 format, u32 flags) {
    wroc_dmabuf_create_buffer(client, params, buffer_id, width, height, format, flags);
}

const struct zwp_linux_buffer_params_v1_interface wroc_zwp_linux_buffer_params_v1_impl = {
    .add          = wroc_dmabuf_params_add,
    .create       = wroc_dmabuf_params_create_buffer,
    .create_immed = wroc_dmabuf_params_create_buffer_immed,
    .destroy      = wroc_simple_resource_destroy_callback,
};

const struct zwp_linux_dmabuf_feedback_v1_interface wroc_zwp_linux_dmabuf_feedback_v1_impl = {
    .destroy = wroc_simple_resource_destroy_callback,
};

void wroc_dma_buffer::on_commit()
{
    lock();
}

void wroc_zwp_linux_dmabuf_v1_bind_global(wl_client* client, void* data, u32 version, u32 id)
{
    auto* new_resource = wl_resource_create(client, &zwp_linux_dmabuf_v1_interface, version, id);
    wroc_resource_set_implementation(new_resource, &wroc_zwp_linux_dmabuf_v1_impl, static_cast<wroc_server*>(data));

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
