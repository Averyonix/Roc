#include "direct/backend.hpp"
#include "wayland/backend.hpp"

ref<wroc_backend> wroc_backend_create(wroc_backend_type type)
{
    ref<wroc_backend> backend = nullptr;
    switch (type) {
        break;case wroc_backend_type::layered:
            backend = core_create<wroc_wayland_backend>();
        break;case wroc_backend_type::direct:
            backend = core_create<wroc_direct_backend>();
        break;default:
            core_debugkill();
    }
    backend->type = type;
    return backend;
}
