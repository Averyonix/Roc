#pragma once

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include <core/signal.hpp>

#include <wm/wm.hpp>

struct UiClient;

auto ui_create(WmServer*, const std::filesystem::path& ini_dir) -> Ref<UiClient>;

// -----------------------------------------------------------------------------

using UiFrameFn = void();

struct UiSignals
{
    Signal<void()> frame;
};
auto ui_get_signals(UiClient*) -> UiSignals&;
void ui_request_frame(UiClient*);
auto ui_get_texture(UiClient*, GpuImage*, GpuSampler*, GpuBlendMode) -> ImTextureID;

auto ui_get_window(ImGuiWindow*) -> WmWindow*;

// -----------------------------------------------------------------------------

template<typename ...Args>
void ui_text(std::format_string<Args...> fmt, Args&&... args)
{
    ImGui::TextUnformatted(std::vformat(fmt.get(), std::make_format_args(args...)).c_str());
}
