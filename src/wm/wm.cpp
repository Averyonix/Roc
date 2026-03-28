#include "wm.hpp"

auto wm_create(Scene* scene) -> Ref<WindowManager>
{
    auto wm = ref_create<WindowManager>();
    wm->scene = scene;

    wm->main_mod = SceneModifier::alt;

    wm_init_movesize(wm.get());

    return wm;
}
