#include "internal.hpp"

auto wm_create(const WindowManagerCreateInfo& info) -> Ref<WindowManager>
{
    auto wm = ref_create<WindowManager>();

    wm->exec = info.exec;
    wm->gpu = info.gpu;
    wm->scene = scene_create(wm->exec, wm->gpu);
    wm->io.context = info.io;

    debug_assert(info.main_mod);
    wm->main_mod = info.main_mod;

    wm->window_system_id = uid_allocate();

    wm_init_io(        wm.get());
    wm_init_seat(      wm.get());
    wm_init_hotkeys(   wm.get());
    wm_init_movesize(  wm.get());
    wm_init_zone(      wm.get());

    return wm;
}

auto wm_get_scene(WindowManager* wm) -> Scene*
{
    return wm->scene.get();
}
