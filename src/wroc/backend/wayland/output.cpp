#include "backend.hpp"

#include "wroc/util.hpp"

#include "wren/wren.hpp"
#include "wren/internal.hpp"

#include "wroc/event.hpp"

wroc_wayland_output* wroc_wayland_backend_find_output_for_surface(wroc_wayland_backend* backend, wl_surface* surface)
{
    for (auto& output : backend->outputs) {
        if (output->wl_surface == surface) return output.get();
    }
    return nullptr;
}

// -----------------------------------------------------------------------------

static
void wroc_listen_wl_callback_done(void*, struct wl_callback*, u32 time);

static
void wroc_register_frame_callback(wroc_wayland_output* output)
{
    output->frame_callback = wl_surface_frame(output->wl_surface);
    constexpr static wl_callback_listener listener {
        .done = wroc_listen_wl_callback_done,
    };
    /* auto res = */wl_callback_add_listener(output->frame_callback, &listener, output);
    wl_surface_commit(output->wl_surface);
    // log_trace("registered: {}", res);
}

static
void wroc_listen_wl_callback_done(void* data, struct wl_callback*, u32 time)
{
    auto* output = static_cast<wroc_wayland_output*>(data);

    // log_trace("wl_callback::done(time = {})", time);

    // if (auto* pointer = output->server->backend->pointer.get()) {
    //     wl_pointer_set_cursor(pointer->wl_pointer, pointer->last_serial, nullptr, 0, 0);
    // }

    wroc_post_event(wroc_output_event {
        .type = wroc_event_type::output_frame,
        .output = output,
    });

    wl_callback_destroy(output->frame_callback);

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

    if (!output->frame_callback) {
        log_warn("  initial configure, registering frame callbacks");
        wroc_register_frame_callback(output);
        wroc_post_event(wroc_output_event {
            .type = wroc_event_type::output_frame,
            .output = output,
        });
    }
}

const xdg_surface_listener wroc_xdg_surface_listener {
    .configure = wroc_listen_xdg_surface_configure,
};

// -----------------------------------------------------------------------------

static constexpr vec2i32 wroc_backend_default_output_size = { 1920, 1080 };

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

    output->desc.modes = {
        {
            .size = output->size,
            .refresh = 0,
        }
    };

    for (auto[i, state] : wroc_to_span<xdg_toplevel_state>(states) | std::views::enumerate) {
        log_debug("  states[{}] = {}", i, magic_enum::enum_name(state));
    }

    if (!output->swapchain) {
        log_debug("Creating vulkan surface");

        auto* backend = dynamic_cast<wroc_wayland_backend*>(server->backend.get());
        auto* wren = server->renderer->wren.get();

        VkSurfaceKHR surface;
        wren_check(wren->vk.CreateWaylandSurfaceKHR(wren->instance, wrei_ptr_to(VkWaylandSurfaceCreateInfoKHR {
            .sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
            .display = backend->wl_display,
            .surface = output->wl_surface,
        }), nullptr, &surface));

        output->swapchain = wren_swapchain_create(wren, surface, server->renderer->output_format);
    }

    wren_swapchain_resize(output->swapchain.get(), output->size);

    wroc_post_event(wroc_output_event {
        .type = wroc_event_type::output_added,
        .output = output,
    });
}

static
void wroc_listen_toplevel_close(void* data, xdg_toplevel*)
{
    auto output = static_cast<wroc_wayland_output*>(data);

    log_debug("xdg_toplevel::close");

    auto* backend = static_cast<wroc_wayland_backend*>(server->backend.get());
    backend->destroy_output(output);

    if (backend->outputs.empty()) {
        log_debug("Last output closed, quitting...");
        wroc_terminate();
    }
}

static
void wroc_listen_toplevel_configure_bounds(void* data, xdg_toplevel*, i32 width, i32 height)
{
    log_debug("xdg_toplevel::configure_bounds");
    log_debug("  bounds = ({}, {})", width, height);
}

static
void wroc_listen_toplevel_wm_capabilities(void* data, xdg_toplevel*, wl_array* capabilities)
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
void wroc_listen_toplevel_decoration_configure(void* data, zxdg_toplevel_decoration_v1*, u32 mode)
{
    if (mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE) {
        log_warn("Parent compositor requested client-side decorations");
    }
}

const zxdg_toplevel_decoration_v1_listener wroc_zxdg_toplevel_decoration_v1_listener {
    .configure = wroc_listen_toplevel_decoration_configure,
};

// -----------------------------------------------------------------------------

#if WROC_BACKEND_RELATIVE_POINTER
void wroc_wayland_backend_update_pointer_constraint(wroc_wayland_output* output)
{
    if (output->locked_pointer) return;

    auto* backend = static_cast<wroc_wayland_backend*>(server->backend.get());
    if (!backend->pointer) {
        log_warn("Could not create pointer constraint, pointer not acquired yet");
        return;
    }

    log_info("Locking pointer...");

    output->locked_pointer = zwp_pointer_constraints_v1_lock_pointer(
        backend->pointer_constraints,
        output->wl_surface,
        backend->pointer->wl_pointer,
        nullptr,
        ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
}
#endif

void wroc_wayland_backend::create_output()
{
    if (!wl_compositor) {
        log_error("No wl_compositor interface bound");
        return;
    }

    if (!xdg_wm_base) {
        log_error("No xdg_wm_base interface bound");
        return;
    }

    auto output = wrei_create<wroc_wayland_output>();

    auto id = next_window_id++;

    output->desc.physical_size_mm = {};
    output->desc.model = "Unknown";
    output->desc.make = "Unknown";
    output->desc.name = std::format("WL-{}", id);
    output->desc.description = std::format("Wayland output {}", id);

    outputs.emplace_back(output);

    output->wl_surface = wl_compositor_create_surface(wl_compositor);
    output->xdg_surface = xdg_wm_base_get_xdg_surface(xdg_wm_base, output->wl_surface);
    xdg_surface_add_listener(output->xdg_surface, &wroc_xdg_surface_listener, output.get());

    output->toplevel = xdg_surface_get_toplevel(output->xdg_surface);
    xdg_toplevel_add_listener(output->toplevel, &wroc_xdg_toplevel_listener, output.get());

    xdg_toplevel_set_app_id(output->toplevel, PROGRAM_NAME);
    xdg_toplevel_set_title(output->toplevel, output->desc.name.c_str());

    if (decoration_manager) {
        output->decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(decoration_manager, output->toplevel);
        zxdg_toplevel_decoration_v1_add_listener(output->decoration, &wroc_zxdg_toplevel_decoration_v1_listener, output.get());
        zxdg_toplevel_decoration_v1_set_mode(output->decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    } else {
        log_warn("Server side decorations are not supported, backend outputs will remain undecorated");
    }

#if WROC_BACKEND_RELATIVE_POINTER
    wroc_wayland_backend_update_pointer_constraint(output.get());
#endif

    wl_surface_commit(output->wl_surface);
}

wroc_wayland_output::~wroc_wayland_output()
{
    auto* wren = server->renderer->wren.get();

    auto surface = swapchain->surface;
    swapchain = nullptr;
    wren->vk.DestroySurfaceKHR(wren->instance, surface, nullptr);

#if WROC_BACKEND_RELATIVE_POINTER
    if (locked_pointer) zwp_locked_pointer_v1_destroy(locked_pointer);
#endif

    if (decoration)  zxdg_toplevel_decoration_v1_destroy(decoration);
    if (toplevel)    xdg_toplevel_destroy(toplevel);
    if (xdg_surface) xdg_surface_destroy(xdg_surface);
    if (wl_surface)  wl_surface_destroy(wl_surface);

    if (frame_callback) wl_callback_destroy(frame_callback);
}

void wroc_wayland_backend::destroy_output(wroc_output* output)
{
    wroc_post_event(wroc_output_event {
        .type = wroc_event_type::output_removed,
        .output = output,
    });

    std::erase_if(outputs, [&](auto& o) { return o.get() == output; });
}
