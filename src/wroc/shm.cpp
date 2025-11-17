#include "server.hpp"

static
void wroc_wl_whm_create_pool(wl_client* client, wl_resource* resource, u32 id, int fd, i32 size)
{
    // TODO: We should enforce a limit on the number of open files a client can have to keep under 1024 for the whole process

    auto* new_resource = wl_resource_create(client, &wl_shm_pool_interface, wl_resource_get_version(resource), id);
    wroc_debug_track_resource(new_resource);
    auto* pool = new wroc_wl_shm_pool {};
    pool->server = wroc_get_userdata<wroc_wl_shm>(resource)->server;
    pool->wl_shm_pool = new_resource;
    pool->fd = fd;
    pool->size = size;
    wroc_resource_set_implementation_refcounted(new_resource, &wroc_wl_shm_pool_impl, pool);
    pool->data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, pool->fd, 0);
    if (pool->data == MAP_FAILED) {
        wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_FD, "mmap failed");
    }
}

const struct wl_shm_interface wroc_wl_shm_impl = {
    .create_pool = wroc_wl_whm_create_pool,
    .release     = wroc_simple_resource_destroy_callback,
};

void wroc_wl_shm_bind_global(wl_client* client, void* data, u32 version, u32 id)
{
    auto* new_resource = wl_resource_create(client, &wl_shm_interface, version, id);
    wroc_debug_track_resource(new_resource);
    auto* shm = new wroc_wl_shm {};
    shm->server = static_cast<wroc_server*>(data);
    shm->wl_shm = new_resource;
    wroc_resource_set_implementation_refcounted(new_resource, &wroc_wl_shm_impl, shm);

    // TODO: Integrate with Wren to expose supported formats

    wl_shm_send_format(new_resource, WL_SHM_FORMAT_ARGB8888);
    wl_shm_send_format(new_resource, WL_SHM_FORMAT_XRGB8888);
};

// -----------------------------------------------------------------------------

static
void wroc_wl_shm_pool_create_buffer(wl_client* client, wl_resource* resource, u32 id, i32 offset, i32 width, i32 height, i32 stride, u32 format)
{
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
    shm_buffer->extent = {width, height};

    shm_buffer->pool = pool;
    shm_buffer->stride = stride;
    shm_buffer->format = wl_shm_format(format);
    shm_buffer->offset = offset;

    wroc_resource_set_implementation_refcounted(new_resource, &wroc_wl_buffer_impl, shm_buffer);

    shm_buffer->image = wren_image_create(shm_buffer->server->renderer->wren.get(), {u32(width), u32(height)}, VK_FORMAT_B8G8R8A8_UNORM);

    log_warn("buffer created ({}, {})", width, height);
}

static
void wroc_wl_shm_pool_resize(wl_client* client, wl_resource* resource, i32 size)
{
    auto* pool = wroc_get_userdata<wroc_wl_shm_pool>(resource);
    munmap(pool->data, pool->size);
    pool->data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, pool->fd, 0);
    pool->size = size;
    if (!pool->data) {
        wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_FD, "mmap failed while resizing pool");
    }
}

const struct wl_shm_pool_interface wroc_wl_shm_pool_impl = {
    .create_buffer = wroc_wl_shm_pool_create_buffer,
    .destroy       = wroc_simple_resource_destroy_callback,
    .resize        = wroc_wl_shm_pool_resize,
};

wroc_wl_shm_pool::~wroc_wl_shm_pool()
{
    if (data) munmap(data, size);
    close(fd);
}

void wroc_shm_buffer::on_commit()
{
    lock();
    wren_image_update(image.get(), static_cast<char*>(pool->data) + offset);
    // log_debug("buffer updated ({}, {})", extent.x, extent.y);
    unlock();
}
