#include "wm.hpp"

auto wm_create(Scene* scene) -> Ref<WindowManager>
{
    auto wm = ref_create<WindowManager>();
    wm->scene = scene;

    wm->main_mod = SceneModifier::alt;

    wm_interaction_init(wm.get());
    wm_zone_init(       wm.get());

    return wm;
}
