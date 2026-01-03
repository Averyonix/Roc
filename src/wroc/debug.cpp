#include <wroc/wroc.hpp>
#include <wroc/event.hpp>

static constexpr int x11_disabled = 0;
static constexpr int x11_enabled = 1;
static constexpr int x11_forced = 2;

struct wroc_debug_gui : wrei_object
{
    bool show_debug_menu = false;
    bool show_log_window = false;
    bool show_demo_window = false;

    struct {
        u32 frames = 0;
        std::chrono::steady_clock::time_point last_report;
        std::chrono::steady_clock::duration frametime;
        f64 fps = 0.0;

        u64 last_events_handled = 0;
        u64 events_per_second = 0;
    } stats;

    struct {
        std::string launch_text = "konsole";
        std::string x11_socket = ":2";
        int x11_mode = x11_disabled;
    } launch;
};
WREI_OBJECT_EXPLICIT_DEFINE(wroc_debug_gui)

void wroc_debug_gui_init(bool show_on_startup)
{
    auto debug = (server->debug_gui = wrei_create<wroc_debug_gui>()).get();

    if (show_on_startup) {
        debug->show_debug_menu = true;
        debug->show_log_window = true;
    }
}

bool wroc_debug_gui_handle_event(wroc_debug_gui* debug, const wroc_event& _event)
{
    if (wroc_event_get_type(_event) == wroc_event_type::keyboard_key) {
        auto& event = static_cast<const wroc_keyboard_event&>(_event);

        if (event.key.upper() == XKB_KEY_A && event.key.pressed && wroc_get_active_modifiers() >= wroc_modifiers::mod) {
            debug->show_debug_menu = !debug->show_debug_menu;
            return true;
        }
    }

    return false;
}

void wroc_imgui_show_debug(wroc_debug_gui* debug)
{
    if (!debug->show_debug_menu) return;

    defer { ImGui::End(); };
    ImGui::Begin(PROJECT_NAME, &debug->show_debug_menu);

    static constexpr float second_column_offset = 113.f;
    static constexpr float third_column_offset = 226.f;

    // Window toggles

    ImGui::Checkbox("Show Log", &debug->show_log_window);

    ImGui::SameLine(second_column_offset);
    ImGui::Checkbox("Show Cursor", &server->renderer->show_debug_cursor);

    ImGui::SameLine(third_column_offset);
    if (ImGui::Button("Quit " PROJECT_NAME)) {
        wroc_terminate();
    }

    ImGui::Checkbox("Show Demo Window", &debug->show_demo_window);

    ImGui::SameLine(third_column_offset);
    if (ImGui::Button("New Output")) {
        server->backend->create_output();
    }

    ImGui::Separator();

    // Focused window toggles

    {
        wroc_surface* surface = server->seat->keyboard->focused_surface.get();
        wroc_toplevel* toplevel = wroc_surface_get_addon<wroc_toplevel>(surface);

        ImGui::BeginDisabled(!toplevel);
        defer { ImGui::EndDisabled(); };

        {
            bool force_rescale = toplevel && toplevel->layout_size.has_value();
            if (ImGui::Checkbox("Rescale", &force_rescale) && toplevel) {
                wroc_toplevel_force_rescale(toplevel, force_rescale);
            }
        }

        ImGui::SameLine(second_column_offset);

        {
            bool fullscreen = toplevel && toplevel->fullscreen.output;
            if (ImGui::Checkbox("Fullscreen", &fullscreen) && toplevel) {
                if (fullscreen) {
                    auto rect = wroc_toplevel_get_layout_rect(toplevel);
                    auto point = rect.origin + rect.extent * 0.5;
                    wroc_output* output;
                    wroc_output_layout_clamp_position(server->output_layout.get(), point, &output);
                    if (output) {
                        wroc_toplevel_set_fullscreen(toplevel, output);
                    }
                } else {
                    wroc_toplevel_set_fullscreen(toplevel, nullptr);
                }
            }
        }

        {
            rect2i32 geom = toplevel ? wroc_xdg_surface_get_geometry(toplevel->base()) : rect2i32{};
            ImGui::SetNextItemWidth(208);
            int size[2] = { geom.extent.x, geom.extent.y };
            ImGui::InputInt2("Geometry", size);

            if (ImGui::IsItemDeactivatedAfterEdit()) {
                size[0] = std::clamp(size[0], 100, 7680);
                size[1] = std::clamp(size[1], 100, 4320);
                wroc_toplevel_set_size(toplevel, vec2i32{size[0], size[1]});
                wroc_toplevel_flush_configure(toplevel);
            }
        }
    }

    ImGui::Separator();

    // Frametime

    {
        auto& stats = debug->stats;

        stats.frames++;
        auto now = std::chrono::steady_clock::now();

        if (now - stats.last_report > 0.5s) {
            auto dur = now - stats.last_report;
            auto seconds = std::chrono::duration_cast<std::chrono::duration<f64>>(dur).count();

            stats.frametime = dur / stats.frames;
            stats.fps = stats.frames / seconds;

            stats.last_report = now;
            stats.frames = 0;

            stats.events_per_second = glm::round((server->event_loop->stats.events_handled - stats.last_events_handled) / seconds);
            stats.last_events_handled = server->event_loop->stats.events_handled;
        }

        ImGui_Text("Frametime:     {} ({:.2f} Hz)", wrei_duration_to_string(stats.frametime), stats.fps);
        ImGui_Text("Events:        {}/s", stats.events_per_second);
    }

    ImGui_Text("Elapsed:       {:.1f}s", wroc_get_elapsed_milliseconds() / 1000.0);

    ImGui::Separator();

    // Allocations

    ImGui_Text("Objects:       {}/{}",
        wrei_registry.active_allocations,
        wrei_registry.inactive_allocations);

    ImGui::Separator();

    auto* wren = server->renderer->wren.get();
    ImGui_Text("Wren.Images:   {} ({} / {})", wren->stats.active_images,
        wrei_byte_size_to_string(wren->stats.active_image_owned_memory),
        wrei_byte_size_to_string(wren->stats.active_image_imported_memory));
    ImGui_Text("Wren.Buffers:  {} ({})", wren->stats.active_buffers,
        wrei_byte_size_to_string(wren->stats.active_buffer_memory));

    ImGui::Separator();

    // Surfaces

    {
        wrei_enum_map<wroc_surface_role, u32> counts = {};
        for (auto* surface : server->surfaces) {
            counts[surface->role]++;
        }

        ImGui_Text("Surfaces:      {}", server->surfaces.size());
        for (auto[role, str] : {
            std::make_pair(wroc_surface_role::none,         "Unassigned:"),
            std::make_pair(wroc_surface_role::cursor,       "Cursor:    "),
            std::make_pair(wroc_surface_role::drag_icon,    "Drag Icon: "),
            std::make_pair(wroc_surface_role::subsurface,   "Subsurface:"),
            std::make_pair(wroc_surface_role::xdg_toplevel, "Toplevel:  "),
            std::make_pair(wroc_surface_role::xdg_popup,    "Popup:     "),
        }) {
            if (auto count = counts[role]) ImGui_Text("  {}  {}", str, count);
            else                           ImGui_Text("  {}",     str);
        }
    }

    ImGui::Separator();

    // Screenshot

    if (ImGui::Button("Screenshot")) {
        server->renderer->screenshot_queued = true;
    }

    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.f, 0.f));

    // Application launch

    auto& launch = debug->launch;

    ImGui::SetNextItemWidth(30.f);
    ImGui::InputText("DISPLAY##x11-socket", &launch.x11_socket);
    ImGui::SameLine(second_column_offset);

    ImGui::SetNextItemWidth(76.f);
    ImGui::Combo("X11", &launch.x11_mode, "Disable\0Enable\0Force\0");

    bool run = ImGui::InputText("##launch", &launch.launch_text, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Run") || run) {
        wroc_spawn("sh", {"sh", "-c", launch.launch_text.c_str()}, {
            wroc_spawn_x11_action{
                launch.x11_mode >= x11_enabled ? launch.x11_socket.c_str() : nullptr,
                launch.x11_mode == x11_forced
            }
        });
    }
}

void wroc_imgui_show_log(wroc_debug_gui* debug)
{
    if (!debug->show_log_window) return;
    defer { ImGui::End(); };
    if (!ImGui::Begin("Log", &debug->show_log_window, ImGuiWindowFlags_NoCollapse)) return;

    auto scroll_to_bottom = ImGui::Button("Follow");

    ImGui::SameLine();
    bool history_enabled = wrei_log_is_history_enabled();
    if (ImGui::Checkbox("Enabled", &history_enabled)) {
        wrei_log_set_history_enabled(history_enabled);
    }

    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        wrei_log_clear_history();
    }

    ImGui::Separator();

    ImGui::BeginChild("scroll-region", ImVec2(), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_HorizontalScrollbar);
    defer { ImGui::EndChild(); };

    auto history = wrei_log_get_history();
    ImGuiListClipper clipper;
    clipper.Begin(history.size());
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const auto& entry = history[i];
            std::optional<ImVec4> color;
            const char* format;
            switch (entry.level) {
                break;case wrei_log_level::trace: format = "[TRACE] %s"; color = ImVec4( 99/255., 104/255., 109/255., 1.0);
                break;case wrei_log_level::debug: format = "[DEBUG] %s"; color = ImVec4( 22/255., 160/255., 133/255., 1.0);
                break;case wrei_log_level::info:  format = " [INFO] %s"; color = ImVec4( 29/255., 153/255., 243/255., 1.0);
                break;case wrei_log_level::warn:  format = " [WARN] %s"; color = ImVec4(253/255., 188/255.,  75/255., 1.0);
                break;case wrei_log_level::error: format = "[ERROR] %s"; color = ImVec4(192/255.,  57/255.,  43/255., 1.0);
                break;case wrei_log_level::fatal: format = "[FATAL] %s"; color = ImVec4(192/255.,  57/255.,  43/255., 1.0);
            }

            if (color) ImGui::PushStyleColor(ImGuiCol_Text, *color);
            ImGui::Text(format, entry.message.c_str());
            if (color) ImGui::PopStyleColor();
        }
    }
    ImGui::Separator();

    if (scroll_to_bottom || (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())) {
        ImGui::SetScrollHereY(1.f);
    }
}

void wroc_debug_gui_frame(wroc_debug_gui* debug)
{
    wroc_imgui_show_debug(debug);
    wroc_imgui_show_log(debug);
    if (debug->show_demo_window) {
        ImGui::ShowDemoWindow(&debug->show_demo_window);
    }
}
