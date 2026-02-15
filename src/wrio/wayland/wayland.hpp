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
};

// -----------------------------------------------------------------------------

struct wrio_output_wayland : wrio_output
{
    WRIO_WL_INTERFACE(wl_surface);
    WRIO_WL_INTERFACE(xdg_surface);
    WRIO_WL_INTERFACE(xdg_toplevel);
    WRIO_WL_INTERFACE(zxdg_toplevel_decoration_v1);
    WRIO_WL_INTERFACE(zwp_locked_pointer_v1);

    virtual void commit() final override;
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
