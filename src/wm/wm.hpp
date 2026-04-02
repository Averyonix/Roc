#pragma once

#include "core/object.hpp"

struct ExecContext;
struct Gpu;
struct Scene;
struct WayServer;
struct WindowManager;
struct IoContext;

struct WindowManagerCreateInfo
{
    ExecContext* exec;
    Gpu*         gpu;
    IoContext*   io;
    Scene*       scene;
    WayServer*   way;

    std::filesystem::path app_share;
    std::filesystem::path wallpaper;
};

auto wm_create(const WindowManagerCreateInfo&) -> Ref<WindowManager>;
