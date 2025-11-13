#include "server.hpp"

void wroc_wl_buffer::lock()
{
    // log_warn("LOCKING BUFFER {}", (void*)this);
    locked = true;
}

void wroc_wl_buffer::unlock()
{
    if (locked) {
        // log_warn("RELEASING BUFFER {}", (void*)this);
        if (wl_buffer) wl_buffer_send_release(wl_buffer);
    }
    locked = false;
}
