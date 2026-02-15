#include "wayland.hpp"

WRIO_WL_LISTENER(wl_seat) = {
    WRIO_WL_STUB(wl_seat, capabilities),
    WRIO_WL_STUB(wl_seat, name),
};
