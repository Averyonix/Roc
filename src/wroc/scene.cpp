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
        if (surface->role == wroc_surface_role::none) continue;

        // Cursor and drag icons don't accept input
        // TODO: Move this into `wroc_surface_point_accepts_input`
        //       (would need to handle subsurfaces for cursors correctly.. theoretically at least)
        if (surface->role == wroc_surface_role::cursor || surface->role == wroc_surface_role::drag_icon) {
            continue;
        }

        auto* surface_under_cursor = surface_accepts_input(surface, pointer->position);
        if (!surface_under_cursor) continue;

        if (p_toplevel) {
            switch (surface->role) {
                break;case wroc_surface_role::xdg_toplevel:
                    if (auto* toplevel = wroc_surface_get_addon<wroc_toplevel>(surface)) {
                        *p_toplevel = toplevel;
                    }
                break;case wroc_surface_role::xdg_popup:
                    if (auto* popup = wroc_surface_get_addon<wroc_popup>(surface)) {
                        *p_toplevel = popup->root_toplevel.get();
                    }
                break;case wroc_surface_role::subsurface:
                    if (auto* subsurface = wroc_surface_get_addon<wroc_subsurface>(surface)) {
                        auto* root = wroc_subsurface_get_root_surface(subsurface);
                        if (auto* toplevel = wroc_surface_get_addon<wroc_toplevel>(root)) {
                            *p_toplevel = toplevel;
                        }
                    }
                break;case wroc_surface_role::cursor:
                      case wroc_surface_role::drag_icon:
                      case wroc_surface_role::none:
                    ;
            }
        }

        return surface_under_cursor;
    }

    return nullptr;
}
