#include "server.hpp"

const struct wl_buffer_interface wroc_wl_buffer_impl = {
    .destroy = wroc_simple_resource_destroy_callback,
};

void wroc_buffer::lock()
{
    // log_warn("LOCKING BUFFER {}", (void*)this);
    locked = true;
}

void wroc_buffer::unlock()
{
    if (locked) {
        // log_warn("RELEASING BUFFER {}", (void*)this);
        if (resource) wl_buffer_send_release(resource);
    }
    locked = false;
}
