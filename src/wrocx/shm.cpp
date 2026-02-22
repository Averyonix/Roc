#include "internal.hpp"

WROC_NAMESPACE_BEGIN

wroc_shm_mapping::~wroc_shm_mapping()
{
    munmap(data, size);
}

static
void update_mapping(wroc_shm_pool* pool, usz size)
{
    auto mapping = wrei_create<wroc_shm_mapping>();
    mapping->size = size;
    mapping->data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, pool->fd->get(), 0);
    if (mapping->data == MAP_FAILED) {
        wroc_post_error(pool->server, pool->resource, WL_SHM_ERROR_INVALID_FD, "mmap failed");
        return;
    }

    pool->mapping = mapping;
}

static
void create_pool(wl_client* client, wl_resource* resource, u32 id, int fd, i32 size)
{
    auto pool = wrei_create<wroc_shm_pool>();
    pool->server = wroc_get_userdata<wroc_server>(resource);
    pool->fd = wrei_fd_adopt(fd);
    pool->resource = wroc_resource_create_refcounted(wl_shm_pool, client, resource, id, pool.get());
    update_mapping(pool.get(), size);
}

WROC_INTERFACE(wl_shm) = {
    .create_pool = create_pool,
    .release = wroc_simple_destroy,
};

WROC_BIND_GLOBAL(wl_shm)
{
    wroc_resource_create(wl_shm, client, version, id, wroc_get_userdata<wroc_server>(data));
}

// -----------------------------------------------------------------------------

inline
wl_shm_format from_drm(wren_drm_format drm)
{
    switch (drm) {
        break;case DRM_FORMAT_XRGB8888: return WL_SHM_FORMAT_XRGB8888;
        break;case DRM_FORMAT_ARGB8888: return WL_SHM_FORMAT_ARGB8888;
        break;default:                  return wl_shm_format(drm);
    }
}

inline
wren_drm_format to_drm(wl_shm_format shm)
{
    switch (shm) {
        break;case WL_SHM_FORMAT_XRGB8888: return DRM_FORMAT_XRGB8888;
        break;case WL_SHM_FORMAT_ARGB8888: return DRM_FORMAT_ARGB8888;
        break;default:                  return wren_drm_format(shm);
    }
}

static
void create_buffer(wl_client* client, wl_resource* resource, u32 id, i32 offset, i32 width, i32 height, i32 stride, u32 _format)
{
    auto* pool = wroc_get_userdata<wroc_shm_pool>(resource);
    auto* server = pool->server;

    auto buffer = wrei_create<wroc_shm_buffer>();
    buffer->server = server;
    buffer->resource = wroc_resource_create_refcounted(wl_buffer, client, resource, id, buffer.get());

    buffer->format = wren_format_from_drm(to_drm(wl_shm_format(_format)));
    buffer->extent = {width, height};
    buffer->pool = pool;
    buffer->stride = stride;
    buffer->offset = offset;

    if (!buffer->format) {
        wroc_post_error(server, resource, WL_SHM_ERROR_INVALID_FORMAT, "Format {} is not supported", wrei_enum_to_string(wl_shm_format(_format)));
        return;
    }

    buffer->image = wren_image_create(server->wren, buffer->extent, buffer->format,
        wren_image_usage::texture | wren_image_usage::transfer);

    log_debug("Created shared buffer {}, format = {}", wrei_to_string(buffer->extent), buffer->format->name);
}

static
void pool_resize(wl_client* client, wl_resource* resource, i32 size)
{
    auto* pool = wroc_get_userdata<wroc_shm_pool>(resource);
    pool->mapping = nullptr;
    update_mapping(pool, size);
}

WROC_INTERFACE(wl_shm_pool) = {
    .create_buffer = create_buffer,
    .destroy = wroc_simple_destroy,
    .resize = pool_resize,
};

wroc_shm_pool::~wroc_shm_pool()
{
    wrei_assert(wrei_allocation_from(mapping.get())->ref_count == 1);
}

// -----------------------------------------------------------------------------

void wroc_shm_buffer::on_commit(wroc_surface* surface)
{
    pending_transfer = true;
}

bool wroc_shm_buffer::is_ready(wroc_surface* surface)
{
    if (pending_transfer) {
        auto mapping = pool->mapping;

        auto queue = wren_get_queue(image->ctx, wren_queue_type::graphics);
        auto commands = wren_commands_begin(queue);

        wren_image_update(commands.get(), image.get(), static_cast<char*>(mapping->data) + offset);

        struct shm_transfer_guard : wrei_object
        {
            ref<wroc_buffer_lock> lock;
            // Protect mapping for duration of transfer
            // This must be destroyed before buffer, in case buffer is holding last reference to shm_pool
            ref<wroc_shm_mapping> mapping;

            ~shm_transfer_guard()
            {
                // Release buffer as soon as transfer has completed
                lock->buffer->release();
            }
        };
        auto transfer_guard = wrei_create<shm_transfer_guard>();
        transfer_guard->lock = lock();
        transfer_guard->mapping = mapping;
        wren_commands_protect_object(commands.get(), transfer_guard.get());

        wren_commands_submit(commands.get(), {});

        pending_transfer = false;
    }

    return true;
}

void wroc_shm_buffer::on_unlock()
{
}

WROC_NAMESPACE_END
