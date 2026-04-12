#include "internal.hpp"

struct WmLinearAccel
{
    f32 offset;
    f32 rate;
    f32 multiplier;

    auto operator()(vec2f32 delta) -> vec2f32
    {
        // Apply a linear mouse acceleration curve
        //
        // Offset     - speed before acceleration is applied.
        // Accel      - rate that sensitivity increases with motion.
        // Multiplier - total multplier for sensitivity.
        //
        //      /
        //     / <- Accel
        // ___/
        //  ^-- Offset

        f32 speed = glm::length(delta);
        vec2f32 sens = vec2f32(multiplier * (1 + (std::max(speed, offset) - offset) * rate));

        return delta * sens;
    }
};

auto wm_get_seat(WindowManager* wm) -> Seat*
{
    return wm->seat.get();
}

void wm_init_seat(WindowManager* wm)
{
    auto seats = scene_get_seats(wm->scene.get());
    debug_assert(!seats.empty());
    wm->seat = seats.front();

    // Pointer

    auto* pointer = seat_get_pointer(wm->seat.get());
    seat_pointer_set_xcursor(pointer, "default");
    seat_pointer_set_accel(  pointer, WmLinearAccel {
        .offset     = 2.f,
        .rate       = 0.05f,
        .multiplier = 0.3f
    });
}
