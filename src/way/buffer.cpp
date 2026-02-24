#include "internal.hpp"

WROC_NAMESPACE_BEGIN

WROC_INTERFACE(wl_buffer) = {
    .destroy = wroc_simple_destroy,
};

[[nodiscard]] ref<wroc_buffer_lock> wroc_buffer::commit(wroc_surface* surface)
{
    if (!released) {
        log_error("Client is attempting to commit a buffer that has not been released!");
    }

    released = false;
    auto guard = lock();

    on_commit(surface);

    return guard;
}

void wroc_buffer::release()
{
    if (released) {
        log_error("Tried to release a buffer that has not been acquired");
        return;
    }

    released = true;

    if (resource) {
        wroc_send(server, wl_buffer_send_release, resource);
    }
}

ref<wroc_buffer_lock> wroc_buffer::lock()
{
    // If buffer is already locked, return existing lock guard
    if (lock_guard) return lock_guard.get();

    // Initial lock *MUST* be acquired when buffer is not released (in control of client)
    wrei_assert(!released);

    // Else create new lock guard
    // Store in a local ref as lock_guard is weak and won't keep it alive to the end of the function
    auto guard = wrei_create<wroc_buffer_lock>();
    guard->buffer = this;
    lock_guard = guard.get();

    return guard;
}

wroc_buffer_lock::~wroc_buffer_lock()
{
    buffer->on_unlock();
}

WROC_NAMESPACE_END
