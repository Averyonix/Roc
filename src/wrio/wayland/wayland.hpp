#include "../internal.hpp"

// -----------------------------------------------------------------------------

#include <wayland-client-core.h>

#include <wayland/client/xdg-shell.h>
#include <wayland/client/xdg-decoration-unstable-v1.h>
#include <wayland/client/relative-pointer-unstable-v1.h>
#include <wayland/client/pointer-constraints-unstable-v1.h>
#include <wayland/client/linux-dmabuf-v1.h>
#include <wayland/client/linux-drm-syncobj-v1.h>

// -----------------------------------------------------------------------------

struct wrio_wayland;
struct wrio_output_wayland;
struct wrio_input_device_wayland_keyboard;
struct wrio_input_device_wayland_pointer;

// -----------------------------------------------------------------------------

template<typename K, typename V, void(*Destroy)(V*)>
struct wrio_wl_proxy_cache
{
    using Vptr = std::unique_ptr<V, decltype([](V* v) { Destroy(v); })>;
    struct entry { weak<K> key; Vptr value; };

    std::vector<entry> entries;

    V* find(K* needle)
    {
        V* found = nullptr;
        std::erase_if(entries, [&](const auto& entry) {
            if (!entry.key) return true;
            if (entry.key.get() == needle) found = entry.value.get();
            return false;
        });
        return found;
    }

    V* insert(K* key, V* value)
    {
        return entries.emplace_back(key, Vptr(value)).value.get();
    }
};

// -----------------------------------------------------------------------------

#define WRIO_WL_INTERFACE(Name) struct Name* Name = {}
#define WRIO_WL_LISTENER(Name) const Name##_listener wrio_##Name##_listener
#define WRIO_WL_STUB(Type, Name) \
    .Name = [](void*, Type* t, auto...) { log_error("TODO - " #Type "{{{}}}::" #Name, (void*) t); }

struct wrio_wayland
{
    WRIO_WL_INTERFACE(wl_display);
    WRIO_WL_INTERFACE(wl_registry);
    WRIO_WL_INTERFACE(wl_compositor);
    WRIO_WL_INTERFACE(xdg_wm_base);
    WRIO_WL_INTERFACE(wl_seat);
    WRIO_WL_INTERFACE(zxdg_decoration_manager_v1);
    WRIO_WL_INTERFACE(zwp_relative_pointer_manager_v1);
    WRIO_WL_INTERFACE(zwp_pointer_constraints_v1);
    WRIO_WL_INTERFACE(zwp_linux_dmabuf_v1);
    WRIO_WL_INTERFACE(wp_linux_drm_syncobj_manager_v1);

    ref<wrei_fd> wl_display_fd = {};

    std::chrono::steady_clock::time_point current_dispatch_time;

    ref<wrio_input_device_wayland_keyboard> keyboard;
    ref<wrio_input_device_wayland_pointer>  pointer;

    wrio_wl_proxy_cache<wren_semaphore, wp_linux_drm_syncobj_timeline_v1, wp_linux_drm_syncobj_timeline_v1_destroy> syncobj_cache;
    wrio_wl_proxy_cache<wren_image,     wl_buffer,                        wl_buffer_destroy>                        buffer_cache;
};

// -----------------------------------------------------------------------------

struct wrio_output_wayland : wrio_output
{
    WRIO_WL_INTERFACE(wl_surface);
    WRIO_WL_INTERFACE(xdg_surface);
    WRIO_WL_INTERFACE(xdg_toplevel);
    WRIO_WL_INTERFACE(zxdg_toplevel_decoration_v1);
    WRIO_WL_INTERFACE(zwp_locked_pointer_v1);
    WRIO_WL_INTERFACE(wp_linux_drm_syncobj_surface_v1);

    wl_callback* frame_callback = {};

    virtual void commit(wren_image*, wren_syncpoint acquire, wren_syncpoint release, flags<wrio_output_commit_flag>) final override;
};

// -----------------------------------------------------------------------------

struct wrio_input_device_wayland_keyboard : wrio_input_device
{
    WRIO_WL_INTERFACE(wl_keyboard);
};

struct wrio_input_device_wayland_pointer : wrio_input_device
{
    WRIO_WL_INTERFACE(wl_pointer);
};

// -----------------------------------------------------------------------------

extern WRIO_WL_LISTENER(xdg_surface);
extern WRIO_WL_LISTENER(xdg_wm_base);
extern WRIO_WL_LISTENER(wl_pointer);
extern WRIO_WL_LISTENER(wl_keyboard);
extern WRIO_WL_LISTENER(wl_seat);
extern WRIO_WL_LISTENER(wl_registry);
extern WRIO_WL_LISTENER(zxdg_toplevel_decoration_v1);
extern WRIO_WL_LISTENER(xdg_toplevel);
extern WRIO_WL_LISTENER(zwp_relative_pointer_v1);
extern WRIO_WL_LISTENER(zwp_locked_pointer_v1);
extern WRIO_WL_LISTENER(zwp_linux_dmabuf_feedback_v1);
