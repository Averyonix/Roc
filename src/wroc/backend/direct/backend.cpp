#include "backend.hpp"

void wroc_direct_backend_init(wroc_server* server)
{
    auto* backend = wrei_get_registry(server)->create<wroc_direct_backend>();
    backend->server = server;
    server->backend = wrei_adopt_ref(backend);

    log_warn("Direct backend is highly experiment and will self terminate after 5 seconds to prevent system lockout");
    std::thread{[] {
        std::this_thread::sleep_for(5s);
        std::terminate();
    }}.detach();

    wroc_backend_init_libinput(backend);
    wroc_backend_init_drm(backend);
}

wroc_direct_backend::~wroc_direct_backend()
{
    wroc_backend_deinit_libinput(this);
}
