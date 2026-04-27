#pragma once

#include "seat.hpp"

#include "../data/data.hpp"
#include "../surface/surface.hpp"
#include "../client.hpp"
#include "../shell/shell.hpp"

static
auto find_surface(WayClient* client, SeatFocus* focus) -> WaySurface*
{
    if (!focus) return nullptr;
    if (focus->client != wm_get_seat_client(client->wm.get())) return nullptr;
    for (auto* surface : client->surfaces) {
        if (surface->scene.focus.get() == focus) return surface;
    }
    return nullptr;
}
