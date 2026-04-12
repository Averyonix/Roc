#include "internal.hpp"

void seat_init(Scene* scene)
{
    auto seat = ref_create<Seat>();
    seat->scene = scene;
    scene->seats.emplace_back(seat.get());

    seat->keyboard = seat_keyboard_create(seat.get());
    seat->pointer = seat_pointer_create(seat.get());
}

auto scene_get_seats(Scene* scene) -> std::span<Seat* const>
{
    return scene->seats;
}

auto seat_get_keyboard(Seat* seat) -> SeatKeyboard*
{
    return seat->keyboard.get();
}

auto seat_get_pointer(Seat* seat) -> SeatPointer*
{
    return seat->pointer.get();
}
