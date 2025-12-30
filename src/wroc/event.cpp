#include "event.hpp"

static
void wroc_handle_event(wroc_server* server, const wroc_event& base_event)
{
    if (server->launcher  && wroc_launcher_handle_event( server->launcher.get(),  base_event)) return;
    if (server->debug_gui && wroc_debug_gui_handle_event(server->debug_gui.get(), base_event)) return;
    if (server->imgui     && wroc_imgui_handle_event(    server->imgui.get(),     base_event)) return;

    if (wroc_handle_zone_interaction(server, base_event)) return;
    if (wroc_handle_focus_cycle_interaction(server, base_event)) return;
    if (wroc_handle_movesize_interaction(server, base_event)) return;

    switch (wroc_event_get_type(base_event)) {
        case wroc_event_type::output_added:
        case wroc_event_type::output_removed:
        case wroc_event_type::output_frame:
            wroc_handle_output_event(server, static_cast<const wroc_output_event&>(base_event));
            break;

        case wroc_event_type::keyboard_key:
        case wroc_event_type::keyboard_modifiers:
            wroc_handle_keyboard_event(server, static_cast<const wroc_keyboard_event&>(base_event));
            break;

        case wroc_event_type::pointer_button:
        case wroc_event_type::pointer_motion:
        case wroc_event_type::pointer_axis:
            wroc_handle_pointer_event(server, static_cast<const wroc_pointer_event&>(base_event));
            break;
    }
}

void wroc_post_event(wroc_server* server, const wroc_event& base_event)
{
    wroc_handle_event(server, base_event);
}
