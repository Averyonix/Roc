#include "wm.hpp"

auto wm_create(scene_context* scene) -> ref<wm_context>
{
    auto wm = core_create<wm_context>();
    wm->scene = scene;

    wm->main_mod = scene_modifier::alt;

    wm_init_movesize(wm.get());

    return wm;
}
