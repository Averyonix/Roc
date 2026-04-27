#include "internal.hpp"

SeatClient::~SeatClient()
{
    // TODO: Allow deletion of client resources in any order safely
    debug_assert(foci.empty());

    std::erase(manager->clients, this);
}

auto seat_connect(SeatManager* manager) -> Ref<SeatClient>
{
    auto client = ref_create<SeatClient>();
    client->manager = manager;
    manager->clients.emplace_back(client.get());
    return client;
}

void seat_listen(SeatClient* client, std::move_only_function<SeatEventHandlerFn>&& event_handler)
{
    client->event_handler = std::move(event_handler);
}
