#pragma once

#include <scene/scene.hpp>
#include <way/way.hpp>
#include <io/io.hpp>
#include <ui/ui.hpp>

struct Shell
{
    ExecContext* exec;
    Gpu* gpu;
    WmServer* wm;
    WayServer* way;
    IoContext* io;
    UiClient* ui;

    SeatModifier main_mod;

    std::filesystem::path app_share;
    std::filesystem::path wallpaper;

    std::string xwayland_socket;
};

void shell_init_xwayland(Shell*, int argc, char* argv[]);
auto shell_init_menu(Shell*) -> Ref<void>;
auto shell_init_launcher(Shell*) -> Ref<void>;
auto shell_init_log_viewer(Shell*) -> Ref<void>;
auto shell_init_background(Shell*) -> Ref<void>;
auto shell_init_io_bridge(Shell*) -> Ref<void>;
