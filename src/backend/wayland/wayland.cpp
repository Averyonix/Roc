#include "backend.hpp"

#include "renderer/renderer.hpp"
#include "renderer/vulkan_helpers.hpp"

#include <vulkan/vulkan_wayland.h>

std::span<const char* const> backend_get_required_instance_extensions(Backend*)
{
    static constexpr std::array extensions {
        VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
    };

    return extensions;
}

// -----------------------------------------------------------------------------

static
void listen_xdg_wm_base_ping(void*, struct xdg_wm_base* xdg_wm_base, u32 serial)
{
    log_trace("xdg_wm_base::ping(serial = {})", serial);

    xdg_wm_base_pong(xdg_wm_base, serial);
}

const xdg_wm_base_listener listeners::xdg_wm_base = {
    .ping = listen_xdg_wm_base_ping,
};

// -----------------------------------------------------------------------------

void listen_registry_global(void *data, wl_registry*, u32 name, const char* interface, u32 version)
{
    auto* backend = static_cast<Backend*>(data);

    bool matched;

#define IF_BIND_INTERFACE(Interface, Member, ...) \
    matched = strcmp(interface, Interface.name) == 0; \
    if (matched) { \
        u32 bind_version = std::min(version, u32(Interface.version)); \
        backend->Member = static_cast<decltype(backend->Member)>(wl_registry_bind(backend->wl_registry, name, &Interface, bind_version)); \
        log_info("wl_registry::global(name = {:2}, interface = {:41}, version = {:2} ({:2}))", name, interface, version, bind_version); \
        { __VA_ARGS__ }\
        break; \
    }

    do {
        IF_BIND_INTERFACE(wl_compositor_interface, wl_compositor)
        IF_BIND_INTERFACE(xdg_wm_base_interface, xdg_wm_base, {
            xdg_wm_base_add_listener(backend->xdg_wm_base, &listeners::xdg_wm_base, backend);
        })
        IF_BIND_INTERFACE(zxdg_decoration_manager_v1_interface, decoration_manager)
        IF_BIND_INTERFACE(wl_seat_interface, seat, {
            wl_seat_add_listener(backend->seat, &listeners::wl_seat, backend);
        })

        log_trace("wl_registry::global(name = {:2}, interface = {:41}, version = {:2})", name, interface, version);
    } while (false);

#undef IF_BIND_INTERFACE
}

void listen_registry_global_remove(void* /* data */, wl_registry*, u32 name)
{
    log_warn("wl_registry::global_remove(name = {:2})", name);
}

const wl_registry_listener listeners::wl_registry {
    .global        = listen_registry_global,
    .global_remove = listen_registry_global_remove,
};

void listen_backend_display_read(void* data, int fd, uint32_t events)
{
    auto* backend = static_cast<Backend*>(data);

    log_trace("backend display read, events = {:#x}", events);

    int res = wl_display_dispatch(backend->wl_display);
    if (res <= 0) {
        log_error("  wl_display_dispatch: {}", res);
        event_loop_remove_fd(backend->display->event_loop, fd);
    }

    log_trace("  done");
}

void backend_init(Display* display)
{
    auto* backend = new Backend{};
    backend->display = display;

    backend->wl_display = wl_display_connect(nullptr);
    backend->wl_registry = wl_display_get_registry(backend->wl_display);

    wl_registry_add_listener(backend->wl_registry, &listeners::wl_registry, backend);
    wl_display_roundtrip(backend->wl_display);

    display->backend = backend;

    event_loop_add_fd(display->event_loop, wl_display_get_fd(backend->wl_display), EPOLLIN, listen_backend_display_read, backend);
    event_loop_add_post_step(display->event_loop, [](void* data) {
        wl_display_flush(static_cast<Backend*>(data)->wl_display);
    }, backend);

    backend_create_output(backend);
}

void backend_destroy(Backend* backend)
{
    delete backend;
}

// -----------------------------------------------------------------------------

struct WaylandOutput : Output
{
    struct wl_surface* wl_surface;
    struct xdg_surface* xdg_surface;
    xdg_toplevel* toplevel;
    zxdg_toplevel_decoration_v1* decoration;
};

// -----------------------------------------------------------------------------

static
void listen_callback_done(void*, struct wl_callback*, uint32_t time);

static
void register_frame_callback(WaylandOutput* output)
{
    auto* callback = wl_surface_frame(output->wl_surface);
    constexpr static wl_callback_listener listener {
        .done = listen_callback_done,
    };
    auto res = wl_callback_add_listener(callback, &listener, output);
    wl_surface_commit(output->wl_surface);
    log_trace("registered: {}", res);
}

void listen_callback_done(void* data, struct wl_callback*, uint32_t time)
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

void backend_create_output(Backend* backend)
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
