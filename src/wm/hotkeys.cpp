#include "internal.hpp"

static
auto close_focused(WindowManager* wm, SceneInputDevice* input_device) -> SceneEventFilterResult
{
    auto mods = scene_seat_get_modifiers(scene_input_device_get_seat(input_device));
    if (!mods.contains(wm->main_mod)) return {};

    auto focus = scene_input_device_get_focus(input_device);
    if (focus && focus->window) {
        scene_window_request_close(focus->window.get());
    }
    return SceneEventFilterResult::capture;
}

static
auto filter_event(WindowManager* wm, SceneEvent* event) -> SceneEventFilterResult
{
    switch (event->type) {
        break;case SceneEventType::keyboard_key:
            if (!event->keyboard.key.pressed) return {};
            if (event->keyboard.key.code == KEY_Q) {
                return close_focused(wm, scene_keyboard_get_base(event->keyboard.keyboard));
            }
            if (event->keyboard.key.code == KEY_S) {
                auto mods = scene_seat_get_modifiers(scene_input_device_get_seat(scene_keyboard_get_base(event->keyboard.keyboard)));
                if (mods.contains(wm->main_mod)) {
                    scene_keyboard_focus(event->keyboard.keyboard, nullptr);
                }
            }
        break;case SceneEventType::pointer_button:
            if (!event->pointer.button.pressed) return {};
            if (event->pointer.button.code == BTN_MIDDLE) {
                return close_focused(wm, scene_pointer_get_base(event->pointer.pointer));
            }
        break;default:
            ;
    }

    return {};
}

void wm_init_hotkeys(WindowManager* wm)
{
    wm->hotkeys.filter = scene_add_input_event_filter(wm->scene, [wm](SceneEvent* event) {
        return filter_event(wm, event);
    });
}
