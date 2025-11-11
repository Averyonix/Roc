#include "protocol.hpp"

#include "compositor/server.hpp"

#define INTERFACE_STUB [](auto...){}

#define CREATE_DUMMY_RESOURCE(InterfaceName, Implementation, Version) \
    log_warn("Creating resource: " #InterfaceName); \
    auto* res = wl_resource_create(client, &InterfaceName##_interface, Version, id); \
    wl_resource_set_implementation(res, &Implementation, nullptr, nullptr)

// -----------------------------------------------------------------------------

const struct wl_compositor_interface impl_wl_compositor = {
    .create_region = INTERFACE_STUB,
    .create_surface = [](struct wl_client *client, struct wl_resource *resource, uint32_t id) {
        CREATE_DUMMY_RESOURCE(wl_surface, impl_wl_surface, wl_surface_interface.version);
    },
};

const wl_global_bind_func_t bind_wl_compositor = [](wl_client* client, void*, uint32_t version, uint32_t id) {
    CREATE_DUMMY_RESOURCE(wl_compositor, impl_wl_compositor, version);
};

const struct wl_surface_interface impl_wl_surface = {
    .destroy = INTERFACE_STUB,
    .attach = INTERFACE_STUB,
    .damage = INTERFACE_STUB,
    .frame = INTERFACE_STUB,
    .set_opaque_region = INTERFACE_STUB,
    .set_input_region = INTERFACE_STUB,
    .commit = INTERFACE_STUB,
    .set_buffer_transform = INTERFACE_STUB,
    .set_buffer_scale = INTERFACE_STUB,
    .damage_buffer = INTERFACE_STUB,
};

// -----------------------------------------------------------------------------

const struct xdg_wm_base_interface impl_xdg_wm_base = {
    .create_positioner = INTERFACE_STUB,
    .destroy = INTERFACE_STUB,
    .get_xdg_surface = [](struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *surface) {
        CREATE_DUMMY_RESOURCE(xdg_surface, impl_xdg_surface, xdg_surface_interface.version);
    },
    .pong = INTERFACE_STUB,
};

const wl_global_bind_func_t bind_xdg_wm_base = [](wl_client* client, void*, uint32_t version, uint32_t id) {
    CREATE_DUMMY_RESOURCE(xdg_wm_base, impl_xdg_wm_base, version);
};

const struct xdg_surface_interface impl_xdg_surface = {
    .destroy = INTERFACE_STUB,
    .get_toplevel = [](struct wl_client *client, struct wl_resource *resource, uint32_t id) {
        CREATE_DUMMY_RESOURCE(xdg_toplevel, impl_xdg_toplevel, xdg_toplevel_interface.version);
    },
    .get_popup = INTERFACE_STUB,
    .set_window_geometry = INTERFACE_STUB,
    .ack_configure = INTERFACE_STUB,
};

const struct xdg_toplevel_interface impl_xdg_toplevel = {
    .destroy = INTERFACE_STUB,
    .set_parent = INTERFACE_STUB,
    .set_title = INTERFACE_STUB,
    .set_app_id = INTERFACE_STUB,
    .show_window_menu = INTERFACE_STUB,
    .move = INTERFACE_STUB,
    .resize = INTERFACE_STUB,
    .set_max_size = INTERFACE_STUB,
    .set_min_size = INTERFACE_STUB,
    .set_maximized = INTERFACE_STUB,
    .unset_maximized = INTERFACE_STUB,
    .set_fullscreen = INTERFACE_STUB,
    .unset_fullscreen = INTERFACE_STUB,
};

// -----------------------------------------------------------------------------

const struct wl_shm_interface impl_wl_shm = {
    .create_pool = [](struct wl_client *client, struct wl_resource *resource, uint32_t id, int32_t fd, int32_t size) {
        CREATE_DUMMY_RESOURCE(wl_shm_pool, impl_wl_shm_pool, wl_shm_pool_interface.version);
    },
    .release = INTERFACE_STUB,
};

const wl_global_bind_func_t bind_wl_shm = [](wl_client* client, void*, uint32_t version, uint32_t id) {
    CREATE_DUMMY_RESOURCE(wl_shm, impl_wl_shm, version);
};

const struct wl_shm_pool_interface impl_wl_shm_pool = {
    .create_buffer = [](struct wl_client *client, struct wl_resource *resource, uint32_t id, int32_t offset, int32_t width, int32_t height, int32_t stride, uint32_t format) {
        CREATE_DUMMY_RESOURCE(wl_buffer, impl_wl_buffer, wl_buffer_interface.version);
    },
    .destroy = INTERFACE_STUB,
    .resize = INTERFACE_STUB,
};

const struct wl_buffer_interface impl_wl_buffer = {
    .destroy = INTERFACE_STUB,
};
