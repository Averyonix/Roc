#include "server.hpp"

#include "wren/wren.hpp"
#include "wren/wren_internal.hpp"

#include "wroc/event.hpp"

const u32 wroc_wl_output_version = 4;

// -----------------------------------------------------------------------------

static void wroc_output_send_configuration(wroc_output*, wl_resource* client_resource, bool initial);

// -----------------------------------------------------------------------------

void wroc_output_layout_init(wroc_server* server)
{
    server->output_layout = wrei_create<wroc_output_layout>();
    server->output_layout->server = server;
}

vec2f64 wroc_output_layout_clamp_position(wroc_output_layout* layout, vec2f64 global_pos)
{
    double closest_dist = INFINITY;
    vec2f64 closest = {};

    for (auto& output : layout->outputs) {
        if (wrei_rect_contains(output->layout_rect, global_pos)) {
            return global_pos;
        } else {
            auto pos = wrei_rect_clamp_point(output->layout_rect, global_pos);
            auto dist = glm::distance(pos, global_pos);
            if (dist < closest_dist) {
                closest = pos;
                closest_dist = dist;
            }
        }
    }

    return closest;
}

static
void wroc_output_update(wroc_output_layout* layout)
{
    // TODO: Proper output layout rules

    bool first = true;
    float x = 0;
    for (auto& output : layout->outputs) {
        if (!first) {
            x -= output->size.x;
        }
        first = false;
        output->layout_rect.origin = {x, 0};
        output->layout_rect.extent = output->size;

        log_error("Output layout rect {}", wrei_to_string(output->layout_rect));
    }

    for (auto* surface : layout->server->surfaces) {
        wroc_output_layout_update_surface(layout, surface);
    }
}

void wroc_output_layout_add_output(wroc_output_layout* layout, wroc_output* output)
{
    if (!weak_container_contains(layout->outputs, output)) {
        log_debug("NEW OUTPUT");
        layout->outputs.emplace_back(output);
    }

    auto* server = layout->server;

    if (output->global) {
        log_debug("Output reconfigured");
        for (auto* client_resource : output->resources) {
            wroc_output_send_configuration(output, client_resource, false);
        }
    } else {
        log_debug("Output added");
        output->global = WROC_SERVER_GLOBAL(server, wl_output, output);
    }

    if (server->imgui && !server->imgui->output) {
        // TODO: This should be set to the PRIMARY output, instead of the first output
        server->imgui->output = output;
    }

    wroc_output_update(layout);
}

void wroc_output_layout_remove_output(wroc_output_layout* layout, wroc_output* output)
{
    std::erase(layout->outputs, output);

    wroc_output_update(layout);

    if (output->global) {
        log_debug("Output removed");
        wl_global_remove(output->global);
    }
}

void wroc_output_layout_update_surface(wroc_output_layout* layout, wroc_surface* surface)
{
    auto enter = [&](wroc_output* output) {
        auto* client = wl_resource_get_client(surface->resource);
        for (auto res : output->resources) {
            if (wroc_resource_get_client(res) == client) {
                log_error("Surface {} entered output: {}", (void*)surface, (void*)res);
                wl_surface_send_enter(surface->resource, res);
                return true;
            }
        }

        return false;
    };

    auto leave = [&](wroc_output* output) {
        auto* client = wl_resource_get_client(surface->resource);
        for (auto res : output->resources) {
            if (wroc_resource_get_client(res) == client) {
                log_error("Surface {} left output: {}", (void*)surface, (void*)res);
                wl_surface_send_leave(surface->resource, res);
            }
        }
    };

    // TODO: Use derived "mapped" state to track surface visibility
    if (!surface->current.buffer) {
        // No surface buffer, leave any outputs that we might be in
        for (auto& output : surface->outputs) {
            leave(output.get());
        }
        surface->outputs.clear();
        return;
    }

    rect2f64 rect = surface->buffer_dst;
    // TODO: Account for xdg_surface geometry
    rect.origin += wroc_surface_get_position(surface);

    // Find new outputs
    for (auto& output : layout->outputs) {
        // log_warn("Checking if rects intersect");
        // log_warn("  output:  {}", wrei_to_string(output->layout_rect));
        // log_warn("  surface: {}", wrei_to_string(rect));
        // log_warn("  = {}", wrei_rect_intersects(output->layout_rect, rect));
        if (wrei_rect_intersects(output->layout_rect, rect)) {
            if (!weak_container_contains(surface->outputs, output.get())) {
                if (enter(output.get())) {
                    // Only add the output if the client has bound the wl_output global
                    surface->outputs.emplace_back(output);
                }
            }
        } else if (weak_container_contains(surface->outputs, output.get())) {
            std::erase(surface->outputs, output.get());
            leave(output.get());
        }
    }

    // Erase removed outputs
    std::erase_if(surface->outputs, [&](const auto& output) {
        if (!weak_container_contains(layout->outputs, output.get())) {
            leave(output.get());
            return true;
        }
        return false;
    });
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
    auto rounded = glm::floor(pos);
    if (remainder) *remainder = pos - rounded;
    return rounded;
}

rect2i32 wroc_output_get_pixel_rect(wroc_output* output, rect2f64 rect, rect2f64* remainder)
{
    auto min = wroc_output_get_pixel_float(output, rect.origin);
    auto max = wroc_output_get_pixel_float(output, rect.origin + rect.extent);
    auto extent = glm::round(max - min);
    auto origin = glm::floor(min);
    if (remainder) {
        *remainder = {
            min - origin,
            max - min - (extent),
        };
    }
    return { origin, extent };
}

static
void wroc_output_init_swapchain(wroc_output* output)
{
    auto* renderer = output->server->renderer.get();
    auto* wren = renderer->wren.get();

    log_debug("Creating vulkan swapchain");
    wren_check(vkwsi_swapchain_create(&output->swapchain, renderer->wren->vkwsi, output->vk_surface));

    wren_check(wren->vk.CreateSemaphore(wren->device, wrei_ptr_to(VkSemaphoreCreateInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = wrei_ptr_to(VkSemaphoreTypeCreateInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
            .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
            .initialValue = 0,
        }),
    }), nullptr, &output->timeline));

    std::vector<VkSurfaceFormatKHR> surface_formats;
    wren_vk_enumerate(surface_formats, wren->vk.GetPhysicalDeviceSurfaceFormatsKHR, wren->physical_device, output->vk_surface);
    bool found = false;
    for (auto& f : surface_formats) {
        // TODO: We need to handle selection of format between outputs and render pipelines
        // if (f.format == VK_FORMAT_R8G8B8A8_SRGB || f.format == VK_FORMAT_B8G8R8A8_SRGB) {
        // if (f.format == VK_FORMAT_R8G8B8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_UNORM) {
        if (f.format == renderer->output_format->vk) {
            output->format = f;
            found = true;
            break;
        }
    }
    if (!found) {
        log_error("Could not find appropriate swapchain format");
        return;
    }

    auto sw_info = vkwsi_swapchain_info_default();
    sw_info.image_sharing_mode = VK_SHARING_MODE_EXCLUSIVE;
    sw_info.image_usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
        // TODO: DONT DO THIS - Use an intermediary buffer or multi-view rendering for screenshots
        | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    sw_info.present_mode = VK_PRESENT_MODE_FIFO_KHR;
    sw_info.min_image_count = 2;
    sw_info.format = output->format.format;
    sw_info.color_space = output->format.colorSpace;
    vkwsi_swapchain_set_info(output->swapchain, &sw_info);
}

static
void wroc_output_send_configuration(wroc_output* output, wl_resource* client_resource, bool initial)
{
    log_debug("Output sending configuration to: {}", (void*)client_resource);

    // TODO: This is simply an approximation that we send for the sake of the protocol
    //       It has no impact with respect to actual output layout
    auto position = glm::round(output->layout_rect.origin);

    log_debug("  position = {}", wrei_to_string(position));
    log_debug("  physical size = {}x{}mm", output->physical_size_mm.x, output->physical_size_mm.y);
    log_debug("  subpixel_layout = {}", magic_enum::enum_name(output->subpixel_layout));
    log_debug("  make = {}", output->make);
    log_debug("  model = {}", output->make);
    log_debug("  mode = {}x{} @ {:}", output->mode.size.x, output->mode.size.y, output->mode.refresh);
    log_debug("  scale = {}", output->scale);
    log_debug("  name = {}", output->name);
    log_debug("  description = {}", output->description);

    wl_output_send_geometry(client_resource,
        position.x, position.y,
        output->physical_size_mm.x, output->physical_size_mm.y,
        output->subpixel_layout,
        output->make.c_str(),
        output->model.c_str(),
        WL_OUTPUT_TRANSFORM_NORMAL);

    wl_output_send_mode(client_resource,
        output->mode.flags,
        output->mode.size.x, output->mode.size.y,
        output->mode.refresh * 1000);

    auto version = wl_resource_get_version(client_resource);

    if (version >= WL_OUTPUT_SCALE_SINCE_VERSION) {
        wl_output_send_scale(client_resource, output->scale);
    }

    if (initial && version >= WL_OUTPUT_NAME_SINCE_VERSION) {
        wl_output_send_name(client_resource, output->name.c_str());
    }

    if (version >= WL_OUTPUT_DESCRIPTION_SINCE_VERSION) {
        wl_output_send_description(client_resource, output->description.c_str());
    }

    if (version >= WL_OUTPUT_DONE_SINCE_VERSION) {
        wl_output_send_done(client_resource);
    }
}

void wroc_wl_output_bind_global(wl_client* client, void* data, u32 version, u32 id)
{
    auto output = static_cast<wroc_output*>(data);
    auto* new_resource = wl_resource_create(client, &wl_output_interface, version, id);
    log_warn("OUTPUT BIND: {}", (void*)new_resource);
    output->resources.emplace_back(new_resource);
    wl_resource_set_implementation(new_resource, &wroc_wl_output_impl, output, nullptr);

    wroc_output_send_configuration(output, new_resource, true);

    // See if any of client's surfaces need to enter the newly bound wl_output
    for (auto* surface : output->server->surfaces) {
        if (wl_resource_get_client(surface->resource) == client) {
            wroc_output_layout_update_surface(output->server->output_layout.get(), surface);
        }
    }
}

static
void wroc_output_added(wroc_output* output)
{
    auto* server = output->server;

    if (!output->swapchain) {
        wroc_output_init_swapchain(output);
    }

    log_debug("OUTPUT ADDED");

    wroc_output_layout_add_output(server->output_layout.get(), output);
}

wroc_output::~wroc_output()
{
    auto* wren = server->renderer->wren.get();

    wren->vk.DestroySemaphore(server->renderer->wren->device, timeline, nullptr);
    if (swapchain) vkwsi_swapchain_destroy(swapchain);
    wren->vk.DestroySurfaceKHR(wren->instance, vk_surface, nullptr);
}

static
void wroc_output_removed(wroc_output* output)
{
    wroc_output_layout_remove_output(output->server->output_layout.get(), output);
}

void wroc_handle_output_event(wroc_server* server, const wroc_output_event& event)
{
    switch (event.type) {
        case wroc_event_type::output_added:   wroc_output_added(    event.output); break;
        case wroc_event_type::output_removed: wroc_output_removed(  event.output); break;
        case wroc_event_type::output_frame:   wroc_render_frame(    event.output); break;
        default: {}
    }
}

static
VkSemaphoreSubmitInfo wroc_output_get_next_submit_info(wroc_output* output)
{
    return VkSemaphoreSubmitInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = output->timeline,
        .value = ++output->timeline_value,
    };
}

vkwsi_swapchain_image wroc_output_acquire_image(wroc_output* output)
{

    auto* wren = output->server->renderer->wren.get();
    vkwsi_swapchain_resize(output->swapchain, {u32(output->size.x), u32(output->size.y)});

    auto timeline_info = wroc_output_get_next_submit_info(output);
    wren_check(vkwsi_swapchain_acquire(&output->swapchain, 1, wren->queue, &timeline_info, 1));
    wren_wait_for_timeline_value(wren, timeline_info);

    return vkwsi_swapchain_get_current(output->swapchain);
}
