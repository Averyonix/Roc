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
    server->cursor = wrei_create<wroc_cursor>();
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

    log_info("Using theme: {}", theme ?: "<xcursor-fallback>");

    static constexpr int xcursor_size = 24;

    setenv("XCURSOR_SIZE", std::to_string(xcursor_size).c_str(), true);

    if (theme) {
        setenv("XCURSOR_THEME", theme, true);
    }

    XcursorImage* image = XcursorLibraryLoadImage("default", theme, xcursor_size);
    defer {
        XcursorImageDestroy(image);
    };
    log_info("  size ({}, {}) hot ({}, {})", image->width, image->height, image->xhot, image->yhot);

    cursor->fallback.image = wren_image_create(server->renderer->wren.get(), {image->width, image->height}, wren_format_from_drm(DRM_FORMAT_ABGR8888));
    wren_image_update(cursor->fallback.image.get(), image->pixels);

    cursor->fallback.hotspot = {image->xhot, image->yhot};
}

void wroc_cursor_set(wroc_cursor* cursor, wl_client* client, wroc_surface* surface, vec2i32 hotspot)
{
    bool created;
    auto* cursor_surface = surface ? wroc_surface_get_or_create_addon<wroc_cursor_surface>(surface, &created) : nullptr;
    if (cursor_surface) {
        log_debug("wroc_cursor_surface {}, hotspot = ({}, {})", created ? "created" : "reused", hotspot.x, hotspot.y);
        cursor_surface->hotspot = hotspot;
    }

    // TODO: Track and update only last focused surface
    //       OR track cursor at the client level directly

    u32 count = 0;
    for (auto* target_surface : cursor->server->surfaces) {
        if (!target_surface->resource) continue;
        if (wl_resource_get_client(target_surface->resource) != client) continue;
        if (target_surface->role == wroc_surface_role::cursor) continue;

        target_surface->cursor = cursor_surface;
        count++;
    }

    log_debug("cursor updated for {} surface(s)", count);
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
