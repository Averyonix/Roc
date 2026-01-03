#include "wroc.hpp"

const struct wl_buffer_interface wroc_wl_buffer_impl = {
    .destroy = wroc_simple_resource_destroy_callback,
};

void wroc_buffer::lock()
{
    // log_warn("LOCKING BUFFER {}", (void*)this);
    locks++;
}

void wroc_buffer::unlock()
{
    assert(locks > 0);
    if (!--locks) {
        // log_warn("RELEASING BUFFER {}", (void*)this);
        wroc_send(wl_buffer_send_release, resource);
    }
}
