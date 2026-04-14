#include "internal.hpp"

auto seat_create(SeatCursorManager* cursor_manager, SceneTree* pointer_root, SceneTree* pointer_layer) -> Ref<Seat>
{
    auto seat = ref_create<Seat>();

    seat->keyboard = seat_keyboard_create(seat.get());
    seat->pointer = seat_pointer_create(seat.get(), cursor_manager, pointer_root, pointer_layer);

    return seat;
}

auto seat_get_keyboard(Seat* seat) -> SeatKeyboard*
{
    return seat->keyboard.get();
}

auto seat_get_pointer(Seat* seat) -> SeatPointer*
{
    return seat->pointer.get();
}

// -----------------------------------------------------------------------------

auto seat_add_input_event_filter(Seat* seat, std::move_only_function<SeatEventFilterResult(SeatEvent*)> fn) -> Ref<SeatEventFilter>
{
    auto filter = ref_create<SeatEventFilter>();
    filter->seat = seat;
    filter->filter = std::move(fn);
    seat->input_event_filters.emplace_back(filter.get());
    return filter;
}

SeatEventFilter::~SeatEventFilter()
{
    if (seat) {
        std::erase(seat->input_event_filters, this);
    }
}
