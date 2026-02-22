#pragma once

#include "wroc.hpp"
#include "util.hpp"

#include "wrei/region.hpp"

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#include <wayland/server/xdg-shell.h>
#include <wayland/server/linux-dmabuf-v1.h>
#include <wayland/server/pointer-gestures-unstable-v1.h>
#include <wayland/server/viewporter.h>
#include <wayland/server/relative-pointer-unstable-v1.h>
#include <wayland/server/pointer-constraints-unstable-v1.h>
#include <wayland/server/xdg-decoration-unstable-v1.h>
#include <wayland/server/server-decoration.h>
#include <wayland/server/cursor-shape-v1.h>
#include <wayland/server/linux-drm-syncobj-v1.h>

WROC_NAMESPACE_BEGIN

// -----------------------------------------------------------------------------

struct wroc_server : wrei_object
{
    wrei_event_loop* event_loop;

    wren_context* wren;
    wrui_context* wrui;

    wl_display*  wl_display;
    ref<wrei_fd> wl_event_loop_fd;
    std::string socket_name;

    ref<wren_sampler> sampler;

    // TODO: Per Wayland client
    ref<wrui_client> client;

    ~wroc_server();
};

// -----------------------------------------------------------------------------

struct wroc_surface;
struct wroc_buffer;
struct wroc_buffer_lock;

// -----------------------------------------------------------------------------

using wroc_commit_id = u32;

enum class wroc_surface_role : u32
{
    none,
    cursor,
    drag_icon,
    subsurface,
    xdg_toplevel,
    xdg_popup,
};

// -----------------------------------------------------------------------------

enum class wroc_surface_committed_state : u32
{
    // wl_surface
    buffer,
    offset,
    opaque_region,
    input_region,
    buffer_transform,
    buffer_scale,

    // wl_subsurface / xdg_toplevel / xdg_popup
    parent,

    // xdg_surface
    geometry,
    acked_serial,

    // xdg_toplevel
    title,
    app_id,
    min_size,
    max_size,

    // wl_subsurface
    subsurface_place,
    subsurface_move,
    parent_commit,
};

struct wroc_subsurface_place
{
    ref<wroc_surface> reference;
    ref<wroc_surface> subsurface;
    bool above;
};

struct wroc_subsurface_move
{
    ref<wroc_surface> subsurface;
    vec2i32 position;
};

struct wroc_surface_state
{
    wroc_commit_id commit;
    std::flat_set<wroc_surface_committed_state> committed;

    struct {
        wroc_resource_list frame_callbacks;
        vec2i32 delta;
        region2i32 opaque_region;
        region2i32 input_region;
    } surface;

    struct {
        ref<wroc_buffer> handle;
        ref<wroc_buffer_lock> lock;
        wl_output_transform transform;
        i32 scale;
    } buffer;

    struct {
        rect2i32 geometry;
        u32 acked_serial;
    } xdg;

    struct {
        wroc_commit_id parent_commit;
        std::vector<wroc_subsurface_place> places;
        std::vector<wroc_subsurface_move> moves;
    } subsurface;

    struct {
        vec2i32 min_size;
        vec2i32 max_size;
        std::string title;
        std::string app_id;
    } toplevel;

    ~wroc_surface_state();
};

struct wroc_surface : wrei_object
{
    wroc_server* server;

    // core
    wroc_resource wl_surface;
    wroc_surface_role role = wroc_surface_role::none;

    // state tracking
    wroc_commit_id last_commit_id;
    wroc_surface_state* pending;
    std::deque<wroc_surface_state> cached;
    wroc_surface_state current;

    // subsurface stacks
    std::vector<wroc_surface*> stack;

    // xdg_surface
    wroc_resource xdg_surface;
    u32 sent_serial;
    u32 acked_serial;

    // xdg_toplevel
    wroc_resource xdg_toplevel;

    // ui representation
    ref<wrui_window> window;
    ref<wrui_texture> texture;
    bool mapped;

    ~wroc_surface();
};

void wroc_subsurface_apply( wroc_surface*, wroc_surface_state&);
void wroc_xdg_surface_apply(wroc_surface*, wroc_surface_state&);
void wroc_toplevel_apply(   wroc_surface*, wroc_surface_state&);

// -----------------------------------------------------------------------------

struct wroc_buffer : wrei_object
{
    friend wroc_buffer_lock;

    wroc_server* server;

    wroc_resource resource;

    vec2u32 extent;
    ref<wren_image> image;

    weak<wroc_buffer_lock> lock_guard;
    [[nodiscard]] ref<wroc_buffer_lock> lock();

    [[nodiscard]] ref<wroc_buffer_lock> commit(wroc_surface*);

    bool released = true;
    void release();

    virtual bool is_ready(wroc_surface*) = 0;

protected:
    virtual void on_commit(wroc_surface*) = 0;
    virtual void on_unlock() = 0;
};

struct wroc_buffer_lock : wrei_object
{
    ref<wroc_buffer> buffer;

    ~wroc_buffer_lock();
};

// -----------------------------------------------------------------------------

struct wroc_shm_pool;

struct wroc_shm_mapping
{
    void* data;
    i32 size;

    ~wroc_shm_mapping();
};

struct wroc_shm_pool : wrei_object
{
    wroc_server* server;

    wroc_resource resource;

    ref<wrei_fd> fd;
    ref<wroc_shm_mapping> mapping;

    ~wroc_shm_pool();
};

// -----------------------------------------------------------------------------

struct wroc_shm_buffer : wroc_buffer
{
    ref<wroc_shm_pool> pool;

    i32 offset;
    i32 stride;
    wren_format format;

    bool pending_transfer;

    virtual bool is_ready(wroc_surface*) final override;

    virtual void on_commit(wroc_surface*) final override;
    virtual void on_unlock() final override;
};

// -----------------------------------------------------------------------------

#define WROC_ADDON_SIMPLE_STATE_REQUEST(Type, Field, Name, Expr, ...) \
    [](wl_client* client, wl_resource* resource, __VA_ARGS__) \
    { \
        auto* surface = wroc_get_userdata<wroc_surface>(resource); \
        surface->pending->Field = Expr; \
        surface->pending->committed.insert(wroc_surface_committed_state::Name); \
    }

/**
 * Convenience macro for applying trivial state elements.
 */
#define WROC_ADDON_SIMPLE_STATE_APPLY(From, To, Field, Name) \
    do { \
        if ((From).committed.contains(wroc_surface_committed_state::Name)) { \
            (To).Field = std::move((From).Field); \
        } \
    } while (false)

// -----------------------------------------------------------------------------

u32 wroc_next_serial(wroc_server* server);

void wroc_queue_client_flush(wroc_server* server);

void wroc_send_(wroc_server* server, const char* fn_name, auto fn, auto&& resource, auto&&... args)
{
    if (resource) {
        fn(resource, args...);
        wroc_queue_client_flush(server);
    } else {
        log_error("Failed to dispatch {}, resource is null", fn_name);
    }
}

#define wroc_send(Server, Fn, Resource, ...) \
    wroc_send_(Server, #Fn, Fn, Resource __VA_OPT__(,) __VA_ARGS__)


template<typename ...Args>
void wroc_post_error(wroc_server* server, wl_resource* resource, u32 code, std::format_string<Args...> fmt, Args&&... args)
{
    if (!resource) return;
    auto message = std::vformat(fmt.get(), std::make_format_args(args...));
    log_error("{}", message);
    wl_resource_post_error(resource, code, "%s", message.c_str());
    wroc_queue_client_flush(server);
}

// -----------------------------------------------------------------------------

void wroc_simple_destroy(wl_client* client, wl_resource* resource);

// -----------------------------------------------------------------------------

wl_global* wroc_global_(wroc_server*, const wl_interface*, i32 version, wl_global_bind_func_t, void* data = nullptr);
#define wroc_global(Server, Interface, ...) \
    wroc_global_(Server, &Interface##_interface, wroc_##Interface##_version, wroc_##Interface##_bind_global __VA_OPT__(,) __VA_ARGS__)

// -----------------------------------------------------------------------------

#define WROC_STUB(Name) \
    .Name = [](wl_client*, wl_resource* resource, auto...) { \
        log_error("TODO - {}{{{}}}::" #Name, wl_resource_get_interface(resource)->name, (void*)resource); \
    }
#define WROC_STUB_QUIET(Name) \
    .Name = [](auto...) {}

#define WROC_INTERFACE(Name) \
    const struct Name##_interface wroc_##Name##_impl

#define WROC_BIND_GLOBAL(Name) void wroc_##Name##_bind_global(wl_client* client, void* data, u32 version, u32 id)

#define WROC_INTERFACE_DECLARE(Name, ...) \
    extern WROC_INTERFACE(Name) \
    __VA_OPT__(; \
        static_assert(std::same_as<decltype(__VA_ARGS__), int>); \
        constexpr u32 wroc_##Name##_version = __VA_ARGS__; \
        WROC_BIND_GLOBAL(Name) \
    )

// -----------------------------------------------------------------------------

wl_resource* wroc_resource_create_(wl_client*, const wl_interface*, int version, int id, const void* impl, wrei_object*, bool refcount);

inline
wl_resource* wroc_resource_create_(wl_client* client, const wl_interface* interface, wl_resource* parent, int id, const void* impl, wrei_object* object, bool refcount)
{
    return wroc_resource_create_(client, interface, wl_resource_get_version(parent), id, impl, object, refcount);
}

#define wroc_resource_create(Name, Client, Version, IdOrResource, Object) \
    wroc_resource_create_(Client, &Name##_interface, Version, IdOrResource, &wroc_##Name##_impl, Object, false)

#define wroc_resource_create_refcounted(Name, Client, Version, IdOrResource, Object) \
    wroc_resource_create_(Client, &Name##_interface, Version, IdOrResource, &wroc_##Name##_impl, Object, true)

// -----------------------------------------------------------------------------

WROC_INTERFACE_DECLARE(wl_compositor, 6);
WROC_INTERFACE_DECLARE(wl_region);
WROC_INTERFACE_DECLARE(wl_surface);

// WROC_INTERFACE_DECLARE(wl_subcompositor, true);
// WROC_INTERFACE_DECLARE(wl_subsurface);

// WROC_INTERFACE_DECLARE(wl_output, 4);

WROC_INTERFACE_DECLARE(xdg_wm_base, 7);
WROC_INTERFACE_DECLARE(xdg_surface);
WROC_INTERFACE_DECLARE(xdg_toplevel);
// WROC_INTERFACE_DECLARE(xdg_positioner);
// WROC_INTERFACE_DECLARE(xdg_popup);

WROC_INTERFACE_DECLARE(wl_buffer);

WROC_INTERFACE_DECLARE(wl_shm, 2);
WROC_INTERFACE_DECLARE(wl_shm_pool);

// -----------------------------------------------------------------------------

WROC_NAMESPACE_END
