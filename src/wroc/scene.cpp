#include "wroc.hpp"

wroc_surface* wroc_get_surface_under_cursor(wroc_toplevel** p_toplevel)
{
    if (p_toplevel) *p_toplevel = nullptr;

    if (server->imgui->wants_mouse) {
        return nullptr;
    }

    auto surface_accepts_input = [&](this auto&& surface_accepts_input, wroc_surface* surface, vec2f64 cursor_pos) -> wroc_surface* {
        if (!surface) return nullptr;
        if (!surface->current.buffer) return nullptr;

        for (auto& s : surface->current.surface_stack | std::views::reverse) {
            if (s.get() == surface) {
                if (wroc_surface_point_accepts_input(s.get(), wroc_surface_pos_from_global(surface, cursor_pos))) {
                    return s.get();
                }
            } else if (auto* under = surface_accepts_input(s.get(), cursor_pos)) {
                return under;
            }
        }
        return nullptr;
    };

    auto* pointer = server->seat->pointer.get();
    for (auto* surface : server->surfaces | std::views::reverse) {

        // Cursor and drag icons don't accept input
        // TODO: Move this into `wroc_surface_point_accepts_input`
        //       (would need to handle subsurfaces for cursors correctly.. theoretically at least)
        if (surface->role == wroc_surface_role::cursor || surface->role == wroc_surface_role::drag_icon) {
            continue;
        }

        auto* surface_under_cursor = surface_accepts_input(surface, pointer->position);
        if (!surface_under_cursor) continue;

        if (auto* toplevel = wroc_surface_get_addon<wroc_toplevel>(surface)) {
            if (p_toplevel) *p_toplevel = toplevel;
            return surface_under_cursor;
        }
        if (auto* popup = wroc_surface_get_addon<wroc_popup>(surface)) {
            if (p_toplevel) *p_toplevel = popup->root_toplevel.get();
            return surface_under_cursor;
        }

        return surface_under_cursor;
    }

    return nullptr;
}
