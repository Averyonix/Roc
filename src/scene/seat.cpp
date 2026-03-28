#include "internal.hpp"

void scene_seat_init(Scene* ctx)
{
    auto seat = ref_create<SceneSeat>();
    seat->ctx = ctx;
    ctx->seats.emplace_back(seat.get());

    seat->keyboard = scene_keyboard_create(seat.get());
    seat->pointer = scene_pointer_create(seat.get());
}

auto scene_get_seats(Scene* ctx) -> std::span<SceneSeat* const>
{
    return ctx->seats;
}

auto scene_get_exclusive_seat(Scene* ctx) -> SceneSeat*
{
    debug_assert(ctx->seats.size() == 1, "TODO: Multi-seat support");
    return ctx->seats.front();
}

auto scene_seat_get_keyboard(SceneSeat* seat) -> SceneKeyboard*
{
    return seat->keyboard.get();
}

auto scene_seat_get_pointer(SceneSeat* seat) -> ScenePointer*
{
    return seat->pointer.get();
}
