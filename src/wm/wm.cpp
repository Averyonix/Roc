#include "internal.hpp"

auto wm_create(const WindowManagerCreateInfo& info) -> Ref<WindowManager>
{
    auto wm = ref_create<WindowManager>();

    wm->exec = info.exec;
    wm->gpu = info.gpu;
    wm->scene = info.scene;
    wm->way = info.way;
    wm->io.context = info.io;

    wm->main_mod = SceneModifier::alt;

    wm_init_io(        wm.get());
    wm_init_seat(      wm.get());
    wm_init_hotkeys(   wm.get());
    wm_init_background(wm.get(), info);
    wm_init_movesize(  wm.get());
    wm_init_zone(      wm.get());
    wm_init_log_viewer(wm.get(), info);
    wm_init_launcher(  wm.get(), info);

    return wm;
}
