#pragma once

#include "ui.hpp"

struct UiViewportData {
    Ref<SceneWindow> window;
    RefVector<SceneMesh> meshes;
    Ref<SceneInputRegion> input_plane;

    // Pending reposition request. Requests are double-buffered so that
    // resizes requested during ImGui frames are handled correctly.
    std::optional<rect2f32> reposition;
};

struct UiContext
{
    Gpu* gpu;
    Scene* scene;

    std::chrono::steady_clock::time_point last_frame = {};

    std::string ini_path;

    Ref<GpuSampler> sampler;
    Ref<SceneClient> client;
    ImGuiContext* context;
    u32 frames_requested = 0;

    struct texture {
        Ref<GpuImage>   image;
        Ref<GpuSampler> sampler;
        GpuBlendMode    blend;
    };
    std::vector<texture> textures;
    Ref<GpuImage> font_image;

    std::move_only_function<UiFrameFn> frame_handler;

    std::flat_set<SceneSeat*> seats;

    SceneKeyboard* keyboard;
    ScenePointer*  pointer;

    struct {
        std::string text;
    } clipboard;

    ~UiContext();
};

void ui_frame(UiContext*);
void ui_handle_key(UiContext*, SceneScancode, bool pressed);
void ui_handle_mods(UiContext*);
void ui_handle_motion(UiContext*);
void ui_handle_button(UiContext*, SceneScancode, bool pressed);
void ui_handle_wheel(UiContext*, vec2f32 delta);
void ui_handle_keyboard_enter(UiContext*, SceneKeyboard*, SceneInputRegion*);
void ui_handle_keyboard_leave(UiContext*);
void ui_handle_pointer_enter(UiContext*, ScenePointer*, SceneInputRegion*);
void ui_handle_pointer_leave(UiContext*);
void ui_handle_output_layout(UiContext*);
