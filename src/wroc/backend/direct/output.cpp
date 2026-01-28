#include "backend.hpp"
#include <wren/internal.hpp>
#include <wroc/event.hpp>

void wroc_direct_backend::create_output()
{
    log_error("DRM backend does not support creating new outputs");
}

void wroc_direct_backend::destroy_output(wroc_output* output)
{
    wroc_post_event(wroc_output_event {
        .type = wroc_event_type::output_removed,
        .output = output,
    });

    std::erase_if(outputs, [&](auto& o) { return o.get() == output; });
}
