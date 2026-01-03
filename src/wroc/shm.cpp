#include "wroc.hpp"

const u32 wroc_wl_shm_version = 2;

static
void wroc_wl_whm_create_pool(wl_client* client, wl_resource* resource, u32 id, int fd, i32 size)
{
    // TODO: We should enforce a limit on the number of open files a client can have to keep under 1024 for the whole process

    auto* new_resource = wroc_resource_create(client, &wl_shm_pool_interface, wl_resource_get_version(resource), id);
    auto* pool = wrei_create_unsafe<wroc_shm_pool>();
    pool->resource = new_resource;
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
    auto* new_resource = wroc_resource_create(client, &wl_shm_interface, version, id);
    wroc_resource_set_implementation(new_resource, &wroc_wl_shm_impl, nullptr);

    for (auto&[format, _] : server->renderer->wren->shm_texture_formats.entries) {
        wroc_send(wl_shm_send_format, new_resource, format->shm);
    }
};

// -----------------------------------------------------------------------------

static
void wroc_wl_shm_pool_create_buffer(wl_client* client, wl_resource* resource, u32 id, i32 offset, i32 width, i32 height, i32 stride, u32 format)
{
    auto* pool = wroc_get_userdata<wroc_shm_pool>(resource);

    i32 needed = stride * height + offset;
    if (needed > pool->size) {
        wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_STRIDE, "buffer mapped storage exceeds pool limits");
        return;
    }

    auto* new_resource = wroc_resource_create(client, &wl_buffer_interface, wl_resource_get_version(resource), id);

    auto* shm_buffer = wrei_create_unsafe<wroc_shm_buffer>();
    shm_buffer->type = wroc_buffer_type::shm;
    shm_buffer->resource = new_resource;
    shm_buffer->extent = {width, height};

    shm_buffer->pool = pool;
    shm_buffer->stride = stride;
    shm_buffer->format = wren_format_from_shm(wl_shm_format(format));
    shm_buffer->offset = offset;

    if (!shm_buffer->format) {
        log_error("Unsupported format: {}", magic_enum::enum_name(wl_shm_format(format)));
        wrei_debugbreak();
    }

    wroc_resource_set_implementation_refcounted(new_resource, &wroc_wl_buffer_impl, shm_buffer);

    shm_buffer->image = wren_image_create(server->renderer->wren.get(), shm_buffer->extent, shm_buffer->format);

    log_warn("shm buffer created {}, format = {}", wrei_to_string(shm_buffer->extent), shm_buffer->format->name);
}

static
void wroc_wl_shm_pool_resize(wl_client* client, wl_resource* resource, i32 size)
{
    auto* pool = wroc_get_userdata<wroc_shm_pool>(resource);
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

wroc_shm_pool::~wroc_shm_pool()
{
    if (data) munmap(data, size);
    close(fd);
}

void wroc_shm_buffer::on_commit()
{
    lock();
    wren_image_update(image.get(), static_cast<char*>(pool->data) + offset);

    // Protect the pool's mapped memory until the copy has completed
    wren_wait_idle(image->ctx);
    unlock();
}

void wroc_shm_buffer::on_replace()
{
}
