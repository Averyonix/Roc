#pragma once

#include "ui.hpp"

struct UiViewportData {
    Ref<WmWindow> window;
    RefVector<SceneMesh> meshes;
    Ref<SeatInputRegion> input_region;

    // Pending reposition request. Requests are double-buffered so that
    // resizes requested during ImGui frames are handled correctly.
    std::optional<rect2f32> reposition;
};

struct Ui
{
    Gpu* gpu;

    WindowManager* wm;

    std::chrono::steady_clock::time_point last_frame = {};

    std::string ini_path;

    Ref<GpuSampler> sampler;
    Ref<SeatClient> client;
    ImGuiContext* context;
    u32 frames_requested = 0;

    struct Texture {
        Ref<GpuImage>   image;
        Ref<GpuSampler> sampler;
        GpuBlendMode    blend;
    };
    std::vector<Texture> textures;
    Ref<GpuImage> font_image;

    std::move_only_function<UiFrameFn> frame_handler;

    std::flat_set<Seat*> seats;

    SeatKeyboard* keyboard;
    SeatPointer*  pointer;

    struct {
        std::string text;
    } clipboard;

    ~Ui();
};

void ui_frame(                Ui*);
void ui_handle_key(           Ui*, SeatInputCode, bool pressed);
void ui_handle_mods(          Ui*);
void ui_handle_motion(        Ui*);
void ui_handle_button(        Ui*, SeatInputCode, bool pressed);
void ui_handle_wheel(         Ui*, vec2f32 delta);
void ui_handle_keyboard_enter(Ui*, SeatKeyboard*, SeatInputRegion*);
void ui_handle_keyboard_leave(Ui*);
void ui_handle_pointer_enter( Ui*, SeatPointer*, SeatInputRegion*);
void ui_handle_pointer_leave( Ui*);
void ui_handle_output_layout( Ui*);

struct UiContextGuard
{
    ImGuiContext* old;

    UiContextGuard(ImGuiContext* imgui)
        : old(ImGui::GetCurrentContext())
    {
        ImGui::SetCurrentContext(imgui);
    }

    ~UiContextGuard()
    {
        ImGui::SetCurrentContext(old);
    }
};
