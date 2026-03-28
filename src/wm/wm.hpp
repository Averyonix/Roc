#pragma once

#include "scene/scene.hpp"

enum class WmMovesizeMode
{
    none,
    move,
    size,
};

struct WindowManager
{
    Scene* scene;

    SceneModifier main_mod;

    struct {
        ScenePointer* pointer;

        Ref<SceneClient> client;
        Weak<SceneWindow> window;

        vec2f32  grab;
        rect2f32 frame;
        vec2f32  relative;

        WmMovesizeMode mode;
    } movesize;
};

auto wm_create(Scene*) -> Ref<WindowManager>;

void wm_init_movesize(WindowManager*);
