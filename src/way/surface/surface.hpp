#pragma once

#include "state.hpp"
#include "region.hpp"

#include "../server.hpp"

#include "gpu/gpu.hpp"

#include <wayland/server/viewporter.h>
#include <wayland/server/xdg-decoration-unstable-v1.h>
#include <wayland/server/server-decoration.h>

struct WaySurface;
struct WayClient;

// -----------------------------------------------------------------------------

enum class WaySurfaceStateComponent : u32
{
    // wl_surface
    buffer,
    opaque_region,
    input_region,
    buffer_transform,
    buffer_scale,

    // wp_viewport
    buffer_source,
    buffer_destination,

    // wl_subsurface / xdg_toplevel / xdg_popup
    parent,
    parent_commit,

    // xdg_surface
    geometry,
    acked_serial,

    // xdg_toplevel
    title,
    app_id,
    min_size,
    max_size,
};

struct WaySubsurfacePlace
{
    Ref<WaySurface> reference;
    Ref<WaySurface> subsurface;
    bool above;
};

struct WaySubsurfaceMove
{
    Ref<WaySurface> subsurface;
    vec2i32 position;
};

struct WayPositioner;
struct WayBuffer;

struct WaySurfaceState : WayState<WaySurfaceStateComponent>
{
    struct {
        WayCommitId commit;
    } parent;

    struct {
        WayResourceList frame_callbacks;
        vec2i32 offset;
        region2f32 opaque_region;
        region2f32 input_region;
        WayDamageRegion damage;
    } surface;

    Ref<WayBuffer>      buffer;
    Ref<GpuImage>       image;
    wl_output_transform buffer_transform;
    i32                 buffer_scale = 1;
    rect2f32            buffer_source;
    vec2i32             buffer_destination;
    WayDamageRegion     buffer_damage;

    struct {
        rect2i32 geometry;
        WaySerial acked_serial;
    } xdg;

    struct {
        std::vector<WaySubsurfacePlace> places;
        std::vector<WaySubsurfaceMove>  moves;
    } subsurface;

    struct {
        vec2i32 min_size;
        vec2i32 max_size;
        std::string title;
        std::string app_id;
    } toplevel;

    ~WaySurfaceState();
};

struct WaySurface : WayObject
{
    WayClient* client;

    Weak<WaySurface> parent;

    // core
    WayResource wl_surface;
    WaySurfaceRole role = WaySurfaceRole::none;

    // state tracking
    WayCommitId last_commit_id;
    WayStateQueue<WaySurfaceState> queue;
    WaySurfaceState current;

    // wl_subsurface
    struct {
        WayResource resource;
        bool synchronized;
    } subsurface;

    // xdg_surface
    WayResource xdg_surface;
    WaySerial sent_serial;
    WaySerial acked_serial;

    // xdg_popup
    struct {
        WayResource resource;
        vec2f32 position;
    } popup;

    // xdg_toplevel
    struct {
        WayResource resource;
        rect2f32 anchor;
        vec2f32 gravity = {1, 1};
        Ref<SceneWindow> window;

        WaySerial pending; // commit response to resize configure is pending
        bool queued;  // new reposition request received while pending
    } toplevel;

    // scene
    struct {
        Ref<SceneTree> tree;
        Ref<SceneTexture> texture;
        Ref<SceneInputRegion> input_region;
    } scene;

    bool mapped;

    ~WaySurface();
};

void way_role_destroy(wl_client*, wl_resource*);

void way_surface_on_redraw(WaySurface*);

void way_viewport_apply(WaySurface*, WaySurfaceState& from);

// -----------------------------------------------------------------------------

void way_subsurface_commit(WaySurface*, WaySurfaceState&);
void way_subsurface_apply( WaySurface*, WaySurfaceState&);

// -----------------------------------------------------------------------------

WAY_INTERFACE_DECLARE(wl_compositor, 6);
WAY_INTERFACE_DECLARE(wl_region);
WAY_INTERFACE_DECLARE(wl_surface);

WAY_INTERFACE_DECLARE(wl_subcompositor, 1);
WAY_INTERFACE_DECLARE(wl_subsurface);

WAY_INTERFACE_DECLARE(wp_viewporter, 1);
WAY_INTERFACE_DECLARE(wp_viewport);

WAY_INTERFACE_DECLARE(zxdg_decoration_manager_v1, 1);
WAY_INTERFACE_DECLARE(zxdg_toplevel_decoration_v1);

WAY_INTERFACE_DECLARE(org_kde_kwin_server_decoration_manager, 1);
WAY_INTERFACE_DECLARE(org_kde_kwin_server_decoration);
