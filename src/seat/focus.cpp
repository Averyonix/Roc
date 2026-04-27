#include "internal.hpp"

SeatFocus::~SeatFocus()
{
    for (auto* seat : client->manager->seats) {
        if (auto* keyboard = seat_get_keyboard(seat)) {
            if (seat_keyboard_get_focus(keyboard) == this) {
                seat_keyboard_focus(keyboard, nullptr);
            }
        }

        if (auto* pointer = seat_get_pointer(seat)) {
            if (seat_pointer_get_focus(pointer) == this) {
                seat_pointer_focus(pointer, nullptr);
            }
        }
    }

    std::erase(client->foci, this);
}

auto seat_focus_create(SeatClient* client, SceneInputRegion* input_region) -> Ref<SeatFocus>
{
    auto focus = ref_create<SeatFocus>();
    focus->input_region = input_region;
    focus->client = client;

    client->foci.emplace_back(focus.get());

    return focus;
}

auto seat_find_focus_for_input_region(SeatManager* manager, SceneInputRegion* input_region) -> SeatFocus*
{
    for (auto* client : manager->clients) {
        for (auto* focus : client->foci) {
            if (focus->input_region.get() == input_region) {
                return focus;
            }
        }
    }
    return nullptr;
}
