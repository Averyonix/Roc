#pragma once

#include "../util.hpp"
#include "../server.hpp"
#include "../surface/state.hpp"

#include <wayland/server/xdg-shell.h>

struct WaySurface;
struct WaySurfaceState;

struct WmWindow;

// -----------------------------------------------------------------------------

enum class WayXdgSurfaceStateComponent
{
    geometry     = 1 << 0,
    acked_serial = 1 << 1,
};

struct WayXdgSurfaceState
{
    Flags<WayXdgSurfaceStateComponent> set;
    WayCommitId commit;

    rect2i32 geometry;
    WaySerial acked_serial;
};

struct WayXdgSurface : WaySurfaceAddon
{
    WayCommitQueue<WayXdgSurfaceState> queue;
    WayXdgSurfaceState current;

    WayResource resource;

    WaySerial sent_serial;
    WaySerial acked_serial;

    virtual void commit(WayCommitId) final override;
    virtual void apply( WayCommitId) final override;

    ~WayXdgSurface();
};

void way_xdg_surface_configure(WaySurface*);

// -----------------------------------------------------------------------------

enum class WayToplevelStateComponent : u32
{
    min_size = 1 << 0,
    max_size = 1 << 1,
    title    = 1 << 2,
    app_id   = 1 << 3
};

struct WayToplevelState
{
    Flags<WayToplevelStateComponent> set;
    WayCommitId commit;

    vec2i32 min_size;
    vec2i32 max_size;
    std::string title;
    std::string app_id;
};

struct WayToplevel : WaySurfaceAddon
{
    WayCommitQueue<WayToplevelState> queue;
    WayToplevelState current;

    WayResource resource;
    rect2f32 anchor;
    vec2f32 gravity = {1, 1};
    Ref<WmWindow> window;

    WaySerial pending; // commit response to resize configure is pending
    bool queued;       // new reposition request received while pending

    virtual void commit(WayCommitId) final override;
    virtual void apply( WayCommitId) final override;

    ~WayToplevel();
};

void way_toplevel_on_map_change(WaySurface*, bool mapped);
void way_toplevel_on_reposition(WaySurface*, rect2f32 frame, vec2f32 gravity);
void way_toplevel_on_close(     WaySurface*);

// -----------------------------------------------------------------------------

struct WayPopup : WaySurfaceAddon
{
    WayResource resource;

    vec2f32 position;

    virtual void commit(WayCommitId) final override;
    virtual void apply( WayCommitId) final override;

    ~WayPopup();
};

void way_create_positioner(wl_client*, wl_resource*, u32 id);
void way_get_popup(        wl_client*, wl_resource*, u32 id,
                           wl_resource* parent, wl_resource* positioner);

// -----------------------------------------------------------------------------

WAY_INTERFACE_DECLARE(xdg_wm_base, 7);
WAY_INTERFACE_DECLARE(xdg_surface);
WAY_INTERFACE_DECLARE(xdg_toplevel);
WAY_INTERFACE_DECLARE(xdg_positioner);
WAY_INTERFACE_DECLARE(xdg_popup);
