#include "protocol.hpp"

#include "wren/wren.hpp"
#include "wren/internal.hpp"

#include "wroc/event.hpp"

const u32 wroc_wl_output_version = 4;

// -----------------------------------------------------------------------------

static void wroc_output_send_configuration(wroc_wl_output*, wl_resource* client_resource, bool initial);

// -----------------------------------------------------------------------------

void wroc_output_layout_init()
{
    auto* layout = (server->output_layout = wrei_create<wroc_output_layout>()).get();

    auto* primary = (layout->primary = wrei_create<wroc_wl_output>()).get();

    // TODO: Configuration
    static constexpr auto name = "DP-1";
    static constexpr vec2i32 size = {3840, 2160};
    static constexpr f64 refresh = 144;
    static constexpr f64 dpi = 137.68;

    static constexpr f64 mm_per_inch = 25.4;
    vec2f64 physical_size_mm = glm::round(vec2f64(size) * (mm_per_inch / dpi));

    primary->desc = {
        .make  = PROJECT_NAME,
        .model = "Display",
        .name  = name,
        .description = std::format("{} {}x{} @ {:.2f}Hz", name, size.x, size.y, refresh),
        .physical_size_mm = physical_size_mm,
        .subpixel = WL_OUTPUT_SUBPIXEL_UNKNOWN,
        .scale = 1.0,
        .modes = {
            {
                .size = size,
                .refresh = refresh,
            }
        }
    };

    primary->global = WROC_GLOBAL(wl_output, primary);
}

vec2f64 wroc_output_layout_clamp_position(wroc_output_layout* layout, vec2f64 global_pos, wroc_output** p_output)
{
    double closest_dist = INFINITY;
    vec2f64 closest = {};
    wroc_output* closest_output = nullptr;

    for (auto& output : layout->outputs) {
        if (wrei_rect_contains(output->layout_rect, global_pos)) {
            closest = global_pos;
            closest_output = output.get();
            break;
        } else {
            auto pos = wrei_rect_clamp_point(output->layout_rect, global_pos);
            auto dist = glm::distance(pos, global_pos);
            if (dist < closest_dist) {
                closest = pos;
                closest_dist = dist;
                closest_output = output.get();
            }
        }
    }

    if (p_output) *p_output = closest_output;
    return closest;
}

wroc_output* wroc_output_layout_output_for_surface(wroc_output_layout* layout, wroc_surface* surface)
{
    auto frame = wroc_surface_get_frame(surface);
    auto centroid = frame.origin + frame.extent * 0.5;
    wroc_output* output = nullptr;
    wroc_output_layout_clamp_position(layout, centroid, &output);
    return output;
}

static
void wroc_output_update(wroc_output_layout* layout)
{
    // TODO: Proper output layout rules

    log_info("Output layout:");

    bool first = true;
    float x = 0;
    for (auto& output : layout->outputs) {
        if (!first) {
            x -= output->size.x;
        }
        first = false;
        output->layout_rect.origin = {x, 0};
        output->layout_rect.extent = output->size;

        log_info("  Output: {}", output->desc.name);
        log_info("    Rect: {}", wrei_to_string(output->layout_rect));
    }

    for (auto* surface : server->surfaces) {
        if (auto* toplevel = wroc_surface_get_addon<wroc_toplevel>(surface)) {
            wroc_toplevel_update_fullscreen_size(toplevel);
        }
    }
}

void wroc_output_layout_add_output(wroc_output_layout* layout, wroc_output* output)
{
    if (!weak_container_contains(layout->outputs, output)) {
        log_debug("NEW OUTPUT");
        layout->outputs.emplace_back(output);
    }

    if (!server->imgui->output) {
        // TODO: This should be set to the PRIMARY output, instead of the first output
        server->imgui->output = output;
    }

    wroc_output_update(layout);
}

void wroc_output_layout_remove_output(wroc_output_layout* layout, wroc_output* output)
{
    std::erase(layout->outputs, output);

    wroc_output_update(layout);
}

// -----------------------------------------------------------------------------

const struct wl_output_interface wroc_wl_output_impl = {
    .release = wroc_simple_resource_destroy_callback,
};

vec2f64 wroc_output_get_pixel_float(wroc_output* output, vec2f64 global_pos)
{
    auto pos = global_pos - output->layout_rect.origin;
    // Convert from layout position to pixel position
    pos = pos * vec2f64(output->size) / output->layout_rect.extent;
    return pos;
}

vec2i32 wroc_output_get_pixel(wroc_output* output, vec2f64 global_pos, vec2f64* remainder)
{
    auto pos = wroc_output_get_pixel_float(output, global_pos);
    // For individual pixels, we floor to treat the position as any point within a given pixel
    auto rounded = glm::floor(pos);
    if (remainder) *remainder = pos - rounded;
    return rounded;
}

rect2i32 wroc_output_get_pixel_rect(wroc_output* output, rect2f64 rect, rect2f64* remainder)
{
    auto min = wroc_output_get_pixel_float(output, rect.origin);
    auto max = wroc_output_get_pixel_float(output, rect.origin + rect.extent);
    // For rects, we round as the min and max are treated as pixel boundaries
    auto extent = glm::round(max - min);
    auto origin = glm::round(min);
    if (remainder) {
        *remainder = {
            min - origin,
            max - min - (extent),
            wrei_xywh,
        };
    }
    return { origin, extent, wrei_xywh };
}

void wroc_output_enter_surface(wroc_wl_output* wl_output, wroc_surface* surface)
{
    for (auto* resource : wl_output->resources) {
        if (wroc_resource_get_client(resource) == wroc_resource_get_client(surface->resource)) {
            wroc_send(wl_surface_send_enter, surface->resource, resource);
        }
    }
}

static
void wroc_output_send_configuration(wroc_wl_output* wl_output, wl_resource* client_resource, bool initial)
{
    log_debug("Output sending configuration to: {}", (void*)client_resource);

    auto& desc = wl_output->desc;

    log_debug("  name = {}", desc.name);
    log_debug("  description = {}", desc.description);
    log_debug("  make = {}", desc.make);
    log_debug("  model = {}", desc.model);
    log_debug("  position = {}", wrei_to_string(wl_output->position));
    log_debug("  physical size = {}x{}mm", desc.physical_size_mm.x, desc.physical_size_mm.y);
    log_debug("  transform = {}", wrei_enum_to_string(desc.transform));
    log_debug("  subpixel_layout = {}", wrei_enum_to_string(desc.subpixel));
    log_debug("  scale = {:.2f}", desc.scale);

    wroc_send(wl_output_send_geometry, client_resource,
        wl_output->position.x, wl_output->position.y,
        desc.physical_size_mm.x, desc.physical_size_mm.y,
        desc.subpixel,
        desc.make.c_str(),
        desc.model.c_str(),
        desc.transform);

    auto& mode = desc.modes[desc.current_mode];
    log_debug("  mode = {}x{} @ {:.2f}Hz", mode.size.x, mode.size.y, mode.refresh);
    wroc_send(wl_output_send_mode, client_resource,
        WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED,
        mode.size.x, mode.size.y,
        mode.refresh * 1000);

    auto version = wl_resource_get_version(client_resource);

    if (version >= WL_OUTPUT_SCALE_SINCE_VERSION) {
        wroc_send(wl_output_send_scale, client_resource, desc.scale);
    }

    if (initial && version >= WL_OUTPUT_NAME_SINCE_VERSION) {
        wroc_send(wl_output_send_name, client_resource, desc.name.c_str());
    }

    if (version >= WL_OUTPUT_DESCRIPTION_SINCE_VERSION) {
        wroc_send(wl_output_send_description, client_resource, desc.description.c_str());
    }

    if (version >= WL_OUTPUT_DONE_SINCE_VERSION) {
        wroc_send(wl_output_send_done, client_resource);
    }
}

void wroc_wl_output_bind_global(wl_client* client, void* data, u32 version, u32 id)
{
    auto* output = static_cast<wroc_wl_output*>(data);
    auto* new_resource = wroc_resource_create(client, &wl_output_interface, version, id);
    log_debug("Output bind: {}", (void*)new_resource);
    output->resources.emplace_back(new_resource);
    wl_resource_set_implementation(new_resource, &wroc_wl_output_impl, output, nullptr);

    wroc_send(wroc_output_send_configuration, output, new_resource, true);

    // Enter all client surfaces
    for (auto* surface : server->surfaces) {
        if (wroc_resource_get_client(surface->resource) == client) {
            wroc_output_enter_surface(output, surface);
        }
    }
}

static
void wroc_output_added(wroc_output* output)
{
    log_debug("Output added");

    wroc_output_layout_add_output(server->output_layout.get(), output);
}

static
void wroc_output_removed(wroc_output* output)
{
    wroc_output_layout_remove_output(server->output_layout.get(), output);
}

void wroc_output_try_dispatch_frame(wroc_output* output)
{
    if (output->image_ready
        && (output->frame_requested || !server->renderer->vsync)
        && output->frames_in_flight < server->renderer->max_frames_in_flight
    )  {
        output->frame_requested = false;
        output->image_ready = false;
        wroc_render_frame(output);
    }
}

void wroc_handle_output_event(const wroc_output_event& event)
{
    switch (event.type) {
        break;case wroc_event_type::output_added:
            wroc_output_added(    event.output);
        break;case wroc_event_type::output_removed:
            wroc_output_removed(  event.output);
        break;case wroc_event_type::output_frame_requested:
            event.output->frame_requested = true;
            wroc_output_try_dispatch_frame(event.output);
        break;case wroc_event_type::output_image_ready:
            event.output->image_ready = true;
            wroc_output_try_dispatch_frame(event.output);
        break;default:
            ;
    }
}
