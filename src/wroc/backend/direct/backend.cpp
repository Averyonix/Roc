#include "backend.hpp"

#define WROC_DIRECT_BACKEND_TIME_LIMIT 0

void wroc_direct_backend_init(wroc_server* server)
{
    auto backend = wrei_create<wroc_direct_backend>();
    backend->server = server;
    server->backend = backend;

#if WROC_DIRECT_BACKEND_TIME_LIMIT
    log_warn("Direct backend is highly experiment and will self terminate after {} seconds to prevent system lockout", WROC_DIRECT_BACKEND_TIME_LIMIT);
    std::thread{[] {
        std::this_thread::sleep_for(operator""s(WROC_DIRECT_BACKEND_TIME_LIMIT));
        std::terminate();
    }}.detach();
#endif

    wroc_backend_init_drm(backend.get());
    wroc_backend_init_libinput(backend.get());
}

wroc_direct_backend::~wroc_direct_backend()
{
    wroc_backend_deinit_libinput(this);

    outputs.clear();
}
