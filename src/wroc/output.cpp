#include "server.hpp"

#include "wren/wren.hpp"
#include "wren/wren_internal.hpp"

#include "wroc/event.hpp"

const u32 wroc_wl_output_version = 4;

const struct wl_output_interface wroc_wl_output_impl = {
    .release = wroc_simple_resource_destroy_callback,
};

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
    sw_info.image_usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
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

    wl_output_send_geometry(client_resource,
        output->position.x, output->position.y,
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
}

void wroc_surface_set_output(wroc_surface* surface, wroc_output* output)
{
    if (surface->output.get() == output) return;

    auto* client = wroc_resource_get_client(surface->resource);

    if (surface->output) {
        for (auto res : surface->output->resources) {
            if (wroc_resource_get_client(res) == client) {
                log_warn("Surface leave output: {}", (void*)res);
                wl_surface_send_leave(surface->resource, res);
            }
        }
    }

    surface->output = output;

    if (!output) return;

    for (auto res : output->resources) {
        if (wroc_resource_get_client(res) == client) {
            log_warn("Surface enter output: {}", (void*)res);
            wl_surface_send_enter(surface->resource, res);
        }
    }
}

static
void wroc_output_added(wroc_output* output)
{
    if (!output->swapchain) {
        wroc_output_init_swapchain(output);
    }

    if (std::ranges::contains(output->server->outputs, output)) {
        log_debug("Output reconfigured");
        for (auto* client_resource : output->resources) {
            wroc_output_send_configuration(output, client_resource, false);
        }
    } else {
        log_debug("Output added");
        output->server->outputs.emplace_back(output);
        output->global = WROC_SERVER_GLOBAL(output->server, wl_output, output);
    }
}

static
void wroc_output_removed(wroc_output* output)
{
    if (output->timeline) {
        output->server->renderer->wren->vk.DestroySemaphore(output->server->renderer->wren->device, output->timeline, nullptr);
    }
    if (output->swapchain) {
        vkwsi_swapchain_destroy(output->swapchain);
    }

    std::erase(output->server->outputs, output);

    if (output->global) {
        log_debug("Output removed");
        wl_global_remove(output->global);
    }

    for (auto* surface : output->server->surfaces) {
        if (surface->output.get() == output) {
            wroc_surface_set_output(surface, nullptr);
        }
    }
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
