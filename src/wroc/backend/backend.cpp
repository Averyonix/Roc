#include "direct/backend.hpp"
#include "wayland/backend.hpp"

void wroc_backend_init(wroc_backend_type type)
{
    switch (type) {
        break;case wroc_backend_type::wayland:
            wroc_wayland_backend_init();
        break;case wroc_backend_type::direct:
            wroc_direct_backend_init();
    }
}
