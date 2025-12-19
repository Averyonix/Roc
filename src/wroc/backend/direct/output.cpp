#include "backend.hpp"
#include <wren/wren_internal.hpp>
#include <wroc/event.hpp>

static
int handle_output_timer(void* data)
{
    auto* output = static_cast<wroc_drm_output*>(data);
    wl_event_source_timer_update(output->timer, 1000 / 144);

    wroc_post_event(output->server, wroc_output_event {
        .type = wroc_event_type::output_frame,
        .output = output,
    });

    return 0;
}

void wroc_backend_init_drm(wroc_direct_backend* backend)
{
    auto wren = backend->server->renderer->wren;

    std::vector<VkDisplayPropertiesKHR> displays;
    wren_vk_enumerate(displays, wren->vk.GetPhysicalDeviceDisplayPropertiesKHR, wren->physical_device);

    if (displays.empty()) {
        log_error("No valid display properties");
        return;
    }

    auto display_props = displays.front();
    auto display = display_props.display;
    log_info("Selecting display: {}", display_props.displayName);

    std::vector<VkDisplayModePropertiesKHR> modes;
    wren_vk_enumerate(modes, wren->vk.GetDisplayModePropertiesKHR, wren->physical_device, display);
    if (modes.empty()) {
        log_error("Cannot find any mode for display");
    }

    auto mode_props = modes.front();
    log_info("Selecting mode: {}x{} @ {}Hz",
        mode_props.parameters.visibleRegion.width,
        mode_props.parameters.visibleRegion.height,
        mode_props.parameters.refreshRate / 1000.f);

    std::vector<VkDisplayPlanePropertiesKHR> planes;
    wren_vk_enumerate(planes, wren->vk.GetPhysicalDeviceDisplayPlanePropertiesKHR, wren->physical_device);

    if (planes.empty()) {
        log_error("Cannot find any plane!");
    }

    u32 plane_index;

    for (auto[i, plane_props] : planes | std::views::enumerate) {

        if (plane_props.currentDisplay != VK_NULL_HANDLE && plane_props.currentDisplay != display) {
            continue;
        }

        std::vector<VkDisplayKHR> supported_displays;
        wren_vk_enumerate(supported_displays, wren->vk.GetDisplayPlaneSupportedDisplaysKHR, wren->physical_device, i);
        for (auto& d : supported_displays) {
            if (d == display) {
                log_error("Found plane: {}", i);
                plane_index = i;
                goto found_plane;
            }
        }
    }
    log_error("Could not find plane");
    return;
found_plane:

    VkDisplayPlaneCapabilitiesKHR plane_caps;
    wren_check(wren->vk.GetDisplayPlaneCapabilitiesKHR(wren->physical_device, mode_props.displayMode, plane_index, &plane_caps));

    VkDisplayPlaneAlphaFlagBitsKHR alpha_mode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
    for (auto& am : {
        VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR,
        VK_DISPLAY_PLANE_ALPHA_GLOBAL_BIT_KHR,
        VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_BIT_KHR,
        VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_PREMULTIPLIED_BIT_KHR,
    }) {
        if (plane_caps.supportedAlpha & am) {
            alpha_mode = am;
            break;
        }
    }

    log_info("Alpha mode: {}", magic_enum::enum_name(alpha_mode));

    auto output = wrei_create<wroc_drm_output>();

    output->physical_size_mm = {display_props.physicalDimensions.width, display_props.physicalDimensions.height};
    output->model = "Unknown";
    output->make = "Unknown";
    output->make = display_props.displayName;
    output->description = display_props.displayName;
    output->size = {mode_props.parameters.visibleRegion.width, mode_props.parameters.visibleRegion.height};
    output->mode.flags = WL_OUTPUT_MODE_CURRENT;
    output->mode.size = output->size;
    output->mode.refresh = mode_props.parameters.refreshRate / 1000.f;

    backend->outputs.emplace_back(output);

    output->server = backend->server;

    wren_check(wren->vk.CreateDisplayPlaneSurfaceKHR(wren->instance, wrei_ptr_to(VkDisplaySurfaceCreateInfoKHR {
        .sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR,
        .displayMode = mode_props.displayMode,
        .planeIndex = plane_index,
        .planeStackIndex = planes[plane_index].currentStackIndex,
        .transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .globalAlpha = 1.f,
        .alphaMode = alpha_mode,
        .imageExtent = {mode_props.parameters.visibleRegion.width, mode_props.parameters.visibleRegion.height},
    }), nullptr, &output->vk_surface));

    wroc_post_event(output->server, wroc_output_event {
        .type = wroc_event_type::output_added,
        .output = output.get(),
    });

    output->timer = wl_event_loop_add_timer(wl_display_get_event_loop(backend->server->display), handle_output_timer, output.get());
    handle_output_timer(output.get());
}

wroc_drm_output::~wroc_drm_output()
{
    wl_event_source_remove(timer);

    auto* wren = server->renderer->wren.get();
    wren->vk.DestroySurfaceKHR(wren->instance, vk_surface, nullptr);
}

void wroc_direct_backend::create_output()
{
    log_error("DRM backend does not support creating new outputs");
}

void wroc_direct_backend::destroy_output(wroc_output* output)
{
    std::erase_if(outputs, [&](auto& o) { return o.get() == output; });
    wrei_remove_ref(output);
}
