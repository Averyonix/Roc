#include "direct/backend.hpp"
#include "wayland/backend.hpp"

ref<wroc_backend> wroc_backend_create(wroc_backend_type type)
{
    ref<wroc_backend> backend = nullptr;
    switch (type) {
        break;case wroc_backend_type::wayland:
            backend = wrei_create<wroc_wayland_backend>();
        break;case wroc_backend_type::direct:
            backend = wrei_create<wroc_direct_backend>();
        break;default:
            wrei_debugkill();
    }
    backend->type = type;
    return backend;
}
