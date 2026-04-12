#include "internal.hpp"

SeatClient::~SeatClient()
{
    // TODO: Allow deletion of client resources in any order safely

    debug_assert(input_regions == 0);

    // Focus must have been dropped before the client can safely be destroyed
    for (auto* seat : scene_get_seats(scene)) {
        debug_assert(scene_get_focus_client(seat->keyboard->focus) != this);
        debug_assert(scene_get_focus_client(seat->pointer->focus)  != this);
    }
}

auto seat_client_create(Scene* scene) -> Ref<SeatClient>
{
    auto client = ref_create<SeatClient>();
    client->scene = scene;
    return client;
}

void seat_client_set_event_handler(SeatClient* client, std::move_only_function<SeatEventHandlerFn>&& event_handler)
{
    client->event_handler = std::move(event_handler);
}

void seat_client_post_event(SeatClient* client, SeatEvent* event)
{
    client->event_handler(event);
}
