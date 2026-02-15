#include "wayland.hpp"

static
auto get_impl(wrio_output* output) -> wrio_output_wayland*
{
    return static_cast<wrio_output_wayland*>(output);
}

WRIO_WL_LISTENER(xdg_surface) = {
    WRIO_WL_STUB(xdg_surface, configure),
};

WRIO_WL_LISTENER(zxdg_toplevel_decoration_v1) = {
    WRIO_WL_STUB(zxdg_toplevel_decoration_v1, configure),
};

WRIO_WL_LISTENER(xdg_toplevel) = {
    WRIO_WL_STUB(xdg_toplevel, configure),
    WRIO_WL_STUB(xdg_toplevel, close),
    WRIO_WL_STUB(xdg_toplevel, configure_bounds),
    WRIO_WL_STUB(xdg_toplevel, wm_capabilities),
};

WRIO_WL_LISTENER(zwp_linux_dmabuf_feedback_v1) = {
    WRIO_WL_STUB(zwp_linux_dmabuf_feedback_v1, done),
    WRIO_WL_STUB(zwp_linux_dmabuf_feedback_v1, format_table),
    WRIO_WL_STUB(zwp_linux_dmabuf_feedback_v1, main_device),
    WRIO_WL_STUB(zwp_linux_dmabuf_feedback_v1, tranche_done),
    WRIO_WL_STUB(zwp_linux_dmabuf_feedback_v1, tranche_target_device),
    WRIO_WL_STUB(zwp_linux_dmabuf_feedback_v1, tranche_formats),
    WRIO_WL_STUB(zwp_linux_dmabuf_feedback_v1, tranche_flags),
};

auto wrio_context_add_output(wrio_context* ctx) -> wrio_output*
{
    auto* wl = ctx->wayland.get();
    wrei_assert(wl);

    static u32 window_id = 0;
    auto title = std::format("WL-{}", ++window_id);
    log_info("Creating new output: {}", title);

    auto output = wrei_create<wrio_output_wayland>();
    ctx->outputs.emplace_back(output);

    output->wl_surface = wl_compositor_create_surface(wl->wl_compositor);
    output->xdg_surface = xdg_wm_base_get_xdg_surface(wl->xdg_wm_base, output->wl_surface);
    xdg_surface_add_listener(output->xdg_surface, &wrio_xdg_surface_listener, output.get());

    output->xdg_toplevel = xdg_surface_get_toplevel(output->xdg_surface);
    xdg_toplevel_add_listener(output->xdg_toplevel, &wrio_xdg_toplevel_listener, output.get());

    xdg_toplevel_set_app_id(output->xdg_toplevel, PROGRAM_NAME);
    xdg_toplevel_set_title(output->xdg_toplevel, title.c_str());

    if (auto decoration_manager = wl->zxdg_decoration_manager_v1) {
        output->zxdg_toplevel_decoration_v1 = zxdg_decoration_manager_v1_get_toplevel_decoration(decoration_manager, output->xdg_toplevel);
        zxdg_toplevel_decoration_v1_add_listener(output->zxdg_toplevel_decoration_v1, &wrio_zxdg_toplevel_decoration_v1_listener, output.get());
        zxdg_toplevel_decoration_v1_set_mode(output->zxdg_toplevel_decoration_v1, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    } else {
        log_warn("WRIO - <zxdg_decoration_manager_v1> protocol not available, outputs will remain undecorated");
    }

    wl_surface_commit(output->wl_surface);
    wl_display_flush(wl->wl_display);

    return output.get();
}

void wrio_output_wayland::commit()
{
    auto impl = get_impl(this);
    log_error("TODO - wrio_output_wayland{{{}}}::commit", (void*)impl);
}
