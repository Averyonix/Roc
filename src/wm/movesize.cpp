#include "internal.hpp"

static
void begin_interaction(WindowManager* wm, ScenePointer* pointer, WmInteractionMode initial_mode)
{
    wm->movesize.pointer = pointer;

    auto pos = scene_pointer_get_position(pointer);
    auto* window = wm_find_window_at(wm, pos);
    if (!window) return;
    auto frame = wm_window_get_frame(window);

    wm->mode = initial_mode;
    wm->movesize.window = window;
    wm->movesize.frame = frame;
    wm->movesize.grab = pos;

    auto dirs = (vec2i32(pos - frame.origin) * 3 / vec2i32(frame.extent)) - 1;

    wm->movesize.relative = {
        dirs.x || !dirs.y,
        dirs.y || !dirs.x,
    };

    if (initial_mode == WmInteractionMode::move && dirs.y < 0) {
        wm->movesize.relative.x = 1;
    } else if (initial_mode == WmInteractionMode::size) {
        if (!dirs.x && !dirs.y) {
            wm->mode = WmInteractionMode::move;
        } else {
            wm->movesize.relative = dirs;
        }
    }
}

static
void end_interaction(WindowManager* wm)
{
    wm->movesize.pointer = nullptr;
    wm->mode = WmInteractionMode::none;
}

// -----------------------------------------------------------------------------

static
void handle_motion(WindowManager* wm)
{
    if (!wm->movesize.window) {
        return;
    }

    auto pos = scene_pointer_get_position(wm->movesize.pointer);
    auto delta = (pos - wm->movesize.grab) * wm->movesize.relative;
    auto frame = wm->movesize.frame;

    if (wm->mode == WmInteractionMode::move) {
        frame.origin += delta;

    } else if (wm->mode == WmInteractionMode::size) {
        delta = glm::max(delta, 100.f - frame.extent);
        frame.origin += glm::min(wm->movesize.relative, {0,0}) * delta;
        frame.extent += delta;
    }

    wm_window_request_reposition(wm->movesize.window.get(), frame, wm->movesize.relative);
}

static
auto filter_event_movesize(WindowManager* wm, SceneEvent* event) -> SceneEventFilterResult
{
    switch (event->type) {
        break;case SceneEventType::pointer_motion:
            if (event->pointer.pointer == wm->movesize.pointer) handle_motion(wm);
        break;case SceneEventType::pointer_button:
            if (event->pointer.pointer == wm->movesize.pointer) {
                if (event->pointer.button.pressed) return SceneEventFilterResult::capture;
                if (scene_pointer_get_pressed(wm->movesize.pointer).empty()) {
                    end_interaction(wm);
                }
            }
        break;case SceneEventType::pointer_scroll:
            if (event->pointer.pointer == wm->movesize.pointer) return SceneEventFilterResult::capture;
        break;default:
            ;
    }

    return {};
}

static
auto filter_event_default(WindowManager* wm, SceneEvent* event) -> SceneEventFilterResult
{
    if (event->type != SceneEventType::pointer_button) return {};

    auto button = event->pointer.button;
    if (!button.pressed) return {};

    auto mods = scene_seat_get_modifiers(scene_input_device_get_seat(scene_pointer_get_base(event->pointer.pointer)));
    if (!mods.contains(wm->main_mod)) return {};

    if (button.code == BTN_LEFT && mods.contains(SceneModifier::shift)) {
        begin_interaction(wm, event->pointer.pointer, WmInteractionMode::move);
        return SceneEventFilterResult::capture;

    } else if (button.code == BTN_RIGHT) {
        begin_interaction(wm, event->pointer.pointer, WmInteractionMode::size);
        return SceneEventFilterResult::capture;
    }

    return {};
}
static

auto filter_event(WindowManager* wm, SceneEvent* event) -> SceneEventFilterResult
{
    switch (wm->mode) {
        break;case WmInteractionMode::none:
            return filter_event_default(wm, event);
        break;case WmInteractionMode::move:
              case WmInteractionMode::size:
            return filter_event_movesize(wm, event);
        break;default:
            ;
    }

    return SceneEventFilterResult::passthrough;
}

void wm_init_movesize(WindowManager* wm)
{
    wm->movesize.filter = scene_add_input_event_filter(wm->scene.get(), [wm](SceneEvent* event) {
        return filter_event(wm, event);
    });
}
