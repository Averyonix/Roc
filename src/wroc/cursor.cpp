#include "server.hpp"

static
std::vector<std::string> wroc_cursor_list_themes()
{
    std::vector<std::string> themes;

    auto* paths_raw = XcursorLibraryPath();
    if (!paths_raw) return themes;

    std::stringstream paths(paths_raw);
    std::string dir;
    while (std::getline(paths, dir, ':')) {
        if (!std::filesystem::exists(dir)) continue;
        for (auto& entry : std::filesystem::directory_iterator(dir)) {
            if (!entry.is_directory()) continue;

            auto theme = entry.path();
            if (       std::filesystem::exists(theme / "cursors")
                    && std::filesystem::exists(theme / "index.theme")) {
                themes.push_back(theme.filename().string());
            }
        }
    }

    return themes;
}

void wroc_cursor_create(wroc_server* server)
{
    server->cursor = wrei_get_registry(server)->create<wroc_cursor>();
    auto cursor = server->cursor.get();
    cursor->server = server;

    auto themes = wroc_cursor_list_themes();
    for (auto& theme : themes) {
        log_info("Found theme: {}", theme);
    }

    const char* theme = nullptr;

    auto try_theme = [&](const char* name) {
        if (theme) return;
        if (std::ranges::contains(themes, name)) theme = name;
    };

    // Prefer breeze > Adwaita
    try_theme("breeze_cursors");
    try_theme("Adwaita");

    // Else fall back to first available theme
    if (!theme && !themes.empty()) theme = themes.front().c_str();

    log_info("Using theme: {}", theme ?: "<xcursor-fallback>");

    XcursorImage* image = XcursorLibraryLoadImage("default", theme, 24);
    log_info("  size ({}, {}) hot ({}, {})", image->width, image->height, image->xhot, image->yhot);

    cursor->fallback.image = wren_image_create(server->renderer->wren.get(), {image->width, image->height}, VK_FORMAT_R8G8B8A8_UNORM);
    wren_image_update(cursor->fallback.image.get(), image->pixels);

    cursor->fallback.hotspot = {image->xhot, image->yhot};
}

void wroc_cursor_set(wroc_cursor* cursor, wroc_surface* surface, vec2i32 hotspot)
{
    if (!surface) {
        cursor->current = nullptr;
        return;
    }

    if (auto* existing = dynamic_cast<wroc_cursor_surface*>(surface->role_addon.get())) {
        cursor->current = existing;
        log_debug("wroc_cursor_surface reused, hotspot = ({}, {})", hotspot.x, hotspot.y);
    } else {
        cursor->current = wrei_adopt_ref(wrei_get_registry(cursor)->create<wroc_cursor_surface>());
        log_debug("wroc_cursor_surface created, hotspot = ({}, {})", hotspot.x, hotspot.y);
    }

    auto cursor_surface = cursor->current.get();
    cursor_surface->surface = surface;
    cursor_surface->surface->role_addon = cursor_surface;

    cursor_surface->hotspot = hotspot;

}

void wroc_cursor_surface::on_commit(wroc_surface_commit_flags)
{
    if (surface->current.committed >= wroc_surface_committed_state::offset) {
        hotspot -= surface->current.delta;

        log_debug("wroc_cursor_surface, delta = ({}, {}), hotspot = ({}, {})",
            surface->current.delta.x,surface->current.delta.y,
            hotspot.x, hotspot.y);
    }
}
