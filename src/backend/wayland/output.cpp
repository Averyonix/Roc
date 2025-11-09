#include "backend.hpp"

#include "renderer/renderer.hpp"
#include "renderer/vulkan_helpers.hpp"

#include <vulkan/vulkan_wayland.h>

WaylandOutput* backend_find_output_for_surface(Backend* backend, wl_surface* surface)
{
    for (auto* output : backend->outputs) {
        if (output->wl_surface == surface) return output;
    }
    return nullptr;
}

// -----------------------------------------------------------------------------

static
void listen_wl_callback_done(void*, struct wl_callback*, u32 time);

static
void register_frame_callback(WaylandOutput* output)
{
    auto* callback = wl_surface_frame(output->wl_surface);
    constexpr static wl_callback_listener listener {
        .done = listen_wl_callback_done,
    };
    auto res = wl_callback_add_listener(callback, &listener, output);
    wl_surface_commit(output->wl_surface);
    log_trace("registered: {}", res);
}

void listen_wl_callback_done(void* data, struct wl_callback*, u32 time)
{
    auto* output = static_cast<WaylandOutput*>(data);

    log_trace("wl_callback::done(time = {})", time);
    output_frame(output);

    // register_frame_callback(output);
}

// -----------------------------------------------------------------------------

static
void listen_xdg_surface_configure(void* data, xdg_surface* surface, u32 serial)
{
    auto* output = static_cast<WaylandOutput*>(data);

    log_debug("xdg_surface::configure");
    log_debug("  serial = {}", serial);

    xdg_surface_ack_configure(surface, serial);

    output_frame(output);
}

const xdg_surface_listener listeners::xdg_surface {
    .configure = listen_xdg_surface_configure,
};

// -----------------------------------------------------------------------------

static constexpr ivec2 backend_default_output_size = { 1280, 720 };

static
void listen_toplevel_configure(void* data, xdg_toplevel*, i32 width, i32 height, wl_array* states)
{
    auto output = static_cast<WaylandOutput*>(data);

    log_debug("xdg_toplevel::configure", width, height);
    log_debug("  size = ({}, {})", width, height);

    if (width == 0 && height == 0) {
        output->size = backend_default_output_size;
    } else {
        output->size = {width, height};
    }

    for (auto[i, state] : to_span<xdg_toplevel_state>(states) | std::views::enumerate) {
        log_debug("  states[{}] = {}", i, magic_enum::enum_name(state));
    }

    if (!output->vk_surface) {
        log_debug("Creating vulkan surface");

        auto* backend = output->display->backend;
        auto* vk = output->display->renderer->vk;
        vk_check(vk->CreateWaylandSurfaceKHR(vk->instance, ptr_to(VkWaylandSurfaceCreateInfoKHR {
            .sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
            .display = backend->wl_display,
            .surface = output->wl_surface,
        }), nullptr, &output->vk_surface));
    }

    if (!output->swapchain) {
        output_init_swapchain(output);
    }

    output_added(output);
}

static
void listen_toplevel_close(void* data, xdg_toplevel*)
{
    auto output = static_cast<WaylandOutput*>(data);
    (void)output;

    log_debug("xdg_toplevel::close");

    output_removed(output);

    auto display = output->display;

    backend_output_destroy(output);

    if (display->backend->outputs.empty()) {
        log_debug("Last output closed, quitting...");
        display_terminate(display);
    }
}

static
void listen_toplevel_configure_bounds(void* /* data */, xdg_toplevel*, i32 width, i32 height)
{
    log_debug("xdg_toplevel::configure_bounds");
    log_debug("  bounds = ({}, {})", width, height);
}

static
void listen_toplevel_wm_capabilities(void* /* data */, xdg_toplevel*, wl_array* capabilities)
{
    log_debug("xdg_toplevel::wm_capabilities");

    for (auto[i, capability] : to_span<xdg_toplevel_state>(capabilities) | std::views::enumerate) {
        log_debug("  capabilities[] = {}", i, magic_enum::enum_name(capability));
    }
}

const xdg_toplevel_listener listeners::xdg_toplevel {
    .configure        = listen_toplevel_configure,
    .close            = listen_toplevel_close,
    .configure_bounds = listen_toplevel_configure_bounds,
    .wm_capabilities  = listen_toplevel_wm_capabilities,
};

// -----------------------------------------------------------------------------

static void listen_toplevel_decoration_configure(void* /* data */, zxdg_toplevel_decoration_v1*, u32 mode)
{
    switch (mode) {
        case ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE:
            break;
        case ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE:
            log_warn("Compositor requested client-side decorations");
            break;
    }
}

const zxdg_toplevel_decoration_v1_listener listeners::zxdg_toplevel_decoration_v1 {
    .configure = listen_toplevel_decoration_configure,
};

// -----------------------------------------------------------------------------

void backend_output_create(Backend* backend)
{
    if (!backend->wl_compositor) {
        log_error("No wl_compositor interface bound");
        return;
    }

    if (!backend->xdg_wm_base) {
        log_error("No xdg_wm_base interface bound");
        return;
    }

    auto* output = new WaylandOutput{};

    backend->outputs.emplace_back(output);

    output->display = backend->display;

    output->wl_surface = wl_compositor_create_surface(backend->wl_compositor);
    output->xdg_surface = xdg_wm_base_get_xdg_surface(backend->xdg_wm_base, output->wl_surface);
    xdg_surface_add_listener(output->xdg_surface, &listeners::xdg_surface, output);

    output->toplevel = xdg_surface_get_toplevel(output->xdg_surface);
    xdg_toplevel_add_listener(output->toplevel, &listeners::xdg_toplevel, output);

    xdg_toplevel_set_app_id(output->toplevel, PROGRAM_NAME);
    xdg_toplevel_set_title(output->toplevel, "WL-1");

    if (backend->decoration_manager) {
        output->decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(backend->decoration_manager, output->toplevel);
        zxdg_toplevel_decoration_v1_add_listener(output->decoration, &listeners::zxdg_toplevel_decoration_v1, output);
        zxdg_toplevel_decoration_v1_set_mode(output->decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    } else {
        log_warn("Server side decorations are not supported, backend outputs will remain undecorated");
    }

    // This will call `wl_surface_commit`
    register_frame_callback(output);
}

void backend_output_destroy(Output* _output)
{
    WaylandOutput* output = static_cast<WaylandOutput*>(_output);

    std::erase(output->display->backend->outputs, output);

    if (output->swapchain) vkwsi_swapchain_destroy(output->swapchain);

    if (output->decoration)  zxdg_toplevel_decoration_v1_destroy(output->decoration);
    if (output->toplevel)    xdg_toplevel_destroy(output->toplevel);
    if (output->xdg_surface) xdg_surface_destroy(output->xdg_surface);
    if (output->wl_surface)  wl_surface_destroy(output->wl_surface);

    delete output;
}
