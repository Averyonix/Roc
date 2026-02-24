#include "internal.hpp"

WAY_INTERFACE(wl_buffer) = {
    .destroy = way_simple_destroy,
};

[[nodiscard]] ref<way_buffer_lock> way_buffer::commit(way_surface* surface)
{
    if (!released) {
        log_error("Client is attempting to commit a buffer that has not been released!");
    }

    released = false;
    auto guard = lock();

    on_commit(surface);

    return guard;
}

void way_buffer::release()
{
    if (released) {
        log_error("Tried to release a buffer that has not been acquired");
        return;
    }

    released = true;

    if (resource) {
        way_send(server, wl_buffer_send_release, resource);
    }
}

ref<way_buffer_lock> way_buffer::lock()
{
    // If buffer is already locked, return existing lock guard
    if (lock_guard) return lock_guard.get();

    // Initial lock *MUST* be acquired when buffer is not released (in control of client)
    core_assert(!released);

    // Else create new lock guard
    // Store in a local ref as lock_guard is weak and won't keep it alive to the end of the function
    auto guard = core_create<way_buffer_lock>();
    guard->buffer = this;
    lock_guard = guard.get();

    return guard;
}

way_buffer_lock::~way_buffer_lock()
{
    buffer->on_unlock();
}
