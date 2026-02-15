#include "wrio.hpp"

static
void handle_event(wrio_event* event)
{
    auto& input = event->input;
    auto& device = input.device;

    switch (event->type) {
        break;case wrio_event_type::input_leave:
            log_info("wrio::input_leave{{{}}}()", (void*)device);
        break;case wrio_event_type::input_key_enter:
            log_info("wrio::input_key_enter{{{}}}({})", (void*)device, libevdev_event_code_get_name(EV_KEY, input.key));
        break;case wrio_event_type::input_key_press:
            log_info("wrio::input_key_press{{{}}}({})", (void*)device, libevdev_event_code_get_name(EV_KEY, input.key));
        break;case wrio_event_type::input_key_release:
            log_info("wrio::input_key_release{{{}}}({})", (void*)device, libevdev_event_code_get_name(EV_KEY, input.key));
        break;case wrio_event_type::input_pointer_motion:
            log_info("wrio::input_pointer_motion{{{}}}{}", (void*)device, wrei_to_string(input.motion));
        break;case wrio_event_type::input_pointer_axis:
            log_info("wrio::input_pointer_axis{{{}}}({}, {})", (void*)device, wrei_enum_to_string(input.axis.axis), input.axis.delta);
        break;case wrio_event_type::input_added:
              case wrio_event_type::input_removed:
              case wrio_event_type::output_added:
              case wrio_event_type::output_removed:
              case wrio_event_type::output_modified:
              case wrio_event_type::output_redraw:
            log_trace("wrio::{}", wrei_enum_to_string(event->type));
    }
}

int main()
{
    auto wrio = wrio_context_create(handle_event);
    wrio_context_run(wrio.get());
}
