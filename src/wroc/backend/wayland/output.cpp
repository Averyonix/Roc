#include "backend.hpp"

#include "wroc/util.hpp"

#include "wren/wren.hpp"

wroc_wayland_output* wroc_backend_find_output_for_surface(wroc_backend* backend, wl_surface* surface)
{
    for (auto* output : backend->outputs) {
        if (output->wl_surface == surface) return output;
    }
    return nullptr;
}

// -----------------------------------------------------------------------------

static
void wroc_listen_wl_callback_done(void*, struct wl_callback*, u32 time);

static
void wroc_register_frame_callback(wroc_wayland_output* output)
{
    auto* callback = wl_surface_frame(output->wl_surface);
    constexpr static wl_callback_listener listener {
        .done = wroc_listen_wl_callback_done,
    };
    /* auto res = */wl_callback_add_listener(callback, &listener, output);
    wl_surface_commit(output->wl_surface);
    // log_trace("registered: {}", res);
}

static
void wroc_listen_wl_callback_done(void* data, struct wl_callback*, u32 time)
{
    auto* output = static_cast<wroc_wayland_output*>(data);

    // log_trace("wl_callback::done(time = {})", time);
    wroc_output_frame(output);

    wroc_register_frame_callback(output);
}

// -----------------------------------------------------------------------------

static
void wroc_listen_xdg_surface_configure(void* data, xdg_surface* surface, u32 serial)
{
    auto* output = static_cast<wroc_wayland_output*>(data);

    log_debug("xdg_surface::configure");
    log_debug("  serial = {}", serial);

    xdg_surface_ack_configure(surface, serial);

    wroc_output_frame(output);
}

const xdg_surface_listener wroc_xdg_surface_listener {
    .configure = wroc_listen_xdg_surface_configure,
};

// -----------------------------------------------------------------------------

static constexpr wrei_vec2i32 wroc_backend_default_output_size = { 1280, 720 };

static
void wroc_listen_toplevel_configure(void* data, xdg_toplevel*, i32 width, i32 height, wl_array* states)
{
    auto output = static_cast<wroc_wayland_output*>(data);

    log_debug("xdg_toplevel::configure", width, height);
    log_debug("  size = ({}, {})", width, height);

    if (width == 0 && height == 0) {
        output->size = wroc_backend_default_output_size;
    } else {
        output->size = {width, height};
    }

    for (auto[i, state] : wroc_to_span<xdg_toplevel_state>(states) | std::views::enumerate) {
        log_debug("  states[{}] = {}", i, magic_enum::enum_name(state));
    }

    if (!output->vk_surface) {
        log_debug("Creating vulkan surface");

        auto* backend = output->server->backend;
        auto* wren = output->server->renderer->wren.get();

        wren_check(wren->vk.CreateWaylandSurfaceKHR(wren->instance, wrei_ptr_to(VkWaylandSurfaceCreateInfoKHR {
            .sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
            .display = backend->wl_display,
            .surface = output->wl_surface,
        }), nullptr, &output->vk_surface));
    }

    wroc_output_added(output);
}

static
void wroc_listen_toplevel_close(void* data, xdg_toplevel*)
{
    auto output = static_cast<wroc_wayland_output*>(data);

    log_debug("xdg_toplevel::close");

    wroc_output_removed(output);

    auto* server = output->server;

    wroc_backend_output_destroy(output);

    if (server->backend->outputs.empty()) {
        log_debug("Last output closed, quitting...");
        wroc_terminate(server);
    }
}

static
void wroc_listen_toplevel_configure_bounds(void* /* data */, xdg_toplevel*, i32 width, i32 height)
{
    log_debug("xdg_toplevel::configure_bounds");
    log_debug("  bounds = ({}, {})", width, height);
}

static
void wroc_listen_toplevel_wm_capabilities(void* /* data */, xdg_toplevel*, wl_array* capabilities)
{
    log_debug("xdg_toplevel::wm_capabilities");

    for (auto[i, capability] : wroc_to_span<xdg_toplevel_state>(capabilities) | std::views::enumerate) {
        log_debug("  capabilities[] = {}", i, magic_enum::enum_name(capability));
    }
}

const xdg_toplevel_listener wroc_xdg_toplevel_listener {
    .configure        = wroc_listen_toplevel_configure,
    .close            = wroc_listen_toplevel_close,
    .configure_bounds = wroc_listen_toplevel_configure_bounds,
    .wm_capabilities  = wroc_listen_toplevel_wm_capabilities,
};

// -----------------------------------------------------------------------------

static
void wroc_listen_toplevel_decoration_configure(void* /* data */, zxdg_toplevel_decoration_v1*, u32 mode)
{
    switch (mode) {
        case ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE:
            break;
        case ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE:
            log_warn("Compositor requested client-side decorations");
            break;
    }
}

const zxdg_toplevel_decoration_v1_listener wroc_zxdg_toplevel_decoration_v1_listener {
    .configure = wroc_listen_toplevel_decoration_configure,
};

// -----------------------------------------------------------------------------

void wroc_backend_output_create(wroc_backend* backend)
{
    if (!backend->wl_compositor) {
        log_error("No wl_compositor interface bound");
        return;
    }

    if (!backend->xdg_wm_base) {
        log_error("No xdg_wm_base interface bound");
        return;
    }

    auto* output = new wroc_wayland_output{};

    backend->outputs.emplace_back(output);

    output->server = backend->server;

    output->wl_surface = wl_compositor_create_surface(backend->wl_compositor);
    output->xdg_surface = xdg_wm_base_get_xdg_surface(backend->xdg_wm_base, output->wl_surface);
    xdg_surface_add_listener(output->xdg_surface, &wroc_xdg_surface_listener, output);

    output->toplevel = xdg_surface_get_toplevel(output->xdg_surface);
    xdg_toplevel_add_listener(output->toplevel, &wroc_xdg_toplevel_listener, output);

    xdg_toplevel_set_app_id(output->toplevel, PROGRAM_NAME);
    xdg_toplevel_set_title(output->toplevel, "WL-1");

    if (backend->decoration_manager) {
        output->decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(backend->decoration_manager, output->toplevel);
        zxdg_toplevel_decoration_v1_add_listener(output->decoration, &wroc_zxdg_toplevel_decoration_v1_listener, output);
        zxdg_toplevel_decoration_v1_set_mode(output->decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    } else {
        log_warn("Server side decorations are not supported, backend outputs will remain undecorated");
    }

    // This will call `wl_surface_commit`
    wroc_register_frame_callback(output);
}

void wroc_backend_output_destroy(wroc_output* _output)
{
    wroc_wayland_output* output = static_cast<wroc_wayland_output*>(_output);

    std::erase(output->server->backend->outputs, output);

    if (output->vk_surface) output->server->renderer->wren->vk.DestroySurfaceKHR(output->server->renderer->wren->instance, output->vk_surface, nullptr);

    if (output->decoration)  zxdg_toplevel_decoration_v1_destroy(output->decoration);
    if (output->toplevel)    xdg_toplevel_destroy(output->toplevel);
    if (output->xdg_surface) xdg_surface_destroy(output->xdg_surface);
    if (output->wl_surface)  wl_surface_destroy(output->wl_surface);

    delete output;
}
