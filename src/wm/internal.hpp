#pragma once

#include "wm.hpp"

#include "core/types.hpp"
#include "scene/scene.hpp"
#include "ui/ui.hpp"
#include "way/way.hpp"
#include "io/io.hpp"

enum class WmInteractionMode
{
    none,
    move,
    size,
    zone,
};

struct WmLauncher;

struct WmOutput {
    Ref<SceneOutput> scene;
    IoOutput*        io;
};

struct WindowManager
{
    ExecContext* exec;
    Gpu*         gpu;
    Scene*       scene;
    WayServer*   way;

    SceneModifier main_mod;

    WmInteractionMode mode;

    Ref<WmLauncher> launcher;

    struct {
        IoContext*            context;
        Ref<GpuImagePool>     pool;
        Ref<SceneClient>      client;
        std::vector<WmOutput> outputs;
    } io;

    struct {
        Ref<SceneClient> client;
    } seat;

    struct {
        Ref<SceneEventFilter> filter;
    } hotkeys;

    struct {
        Ref<SceneClient> client;
        Ref<GpuImage>    image;
        Ref<GpuSampler>  sampler;
        Ref<SceneTree>   layer;
    } background;

    struct {
        Ref<SceneEventFilter> filter;
        ScenePointer* pointer;

        Weak<SceneWindow> window;
        vec2f32  grab;
        rect2f32 frame;
        vec2f32  relative;
    } movesize;

    struct {
        Ref<SceneEventFilter> filter;
        ScenePointer* pointer;
        Ref<SceneTexture> texture;

        Weak<SceneWindow> window;
        aabb2f64 initial_zone;
        aabb2f64 final_zone;
        bool     selecting = false;
    } zone;

    struct {
        Ref<Ui> ui;
        bool requested;
        bool show_details;
        i64 selected = -1;
    } log;
};

void wm_init_io(        WindowManager*);
void wm_init_seat(      WindowManager*);
void wm_init_hotkeys(   WindowManager*);
void wm_init_movesize(  WindowManager*);
void wm_init_zone(      WindowManager*);
void wm_init_log_viewer(WindowManager*, const WindowManagerCreateInfo&);
void wm_init_launcher(  WindowManager*, const WindowManagerCreateInfo&);
void wm_init_background(WindowManager*, const WindowManagerCreateInfo&);
