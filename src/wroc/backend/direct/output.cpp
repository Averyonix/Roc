#include "backend.hpp"
#include <wren/wren_internal.hpp>
#include <wroc/event.hpp>

static
void scanout_thread(std::stop_token stop, wren_context* wren, wroc_drm_output* output)
{
    for (;;) {
        VkFence fence;
        wren_check(wren->vk.RegisterDisplayEventEXT(wren->device, output->vk_display, wrei_ptr_to(VkDisplayEventInfoEXT {
            .sType = VK_STRUCTURE_TYPE_DISPLAY_EVENT_INFO_EXT,
            .displayEvent = VK_DISPLAY_EVENT_TYPE_FIRST_PIXEL_OUT_EXT,
        }), nullptr, &fence));

        wren_check(wren->vk.WaitForFences(wren->device, 1, &fence, true, UINT64_MAX));

        auto now = std::chrono::steady_clock::now();
        output->scanout_time = now;
#if WROC_NOISY_FRAME_TIME
        log_debug("Scanout 1 [{:.3f}]", std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(now.time_since_epoch()).count());
#endif

        wren->vk.DestroyFence(wren->device, fence, nullptr);

        if (stop.stop_requested()) {
            return;
        }

        u64 inc = 1;
        write(output->eventfd, &inc, sizeof(inc));
    }
}

static
int handle_display_scanout(int fd, u32 mask, void* data)
{
    auto* output = static_cast<wroc_drm_output*>(data);

    if (!(mask & WL_EVENT_READABLE)) return 0;

    u64 value = 0;
    while (read(fd, &value, sizeof(value)) > 0)
        ;

#if WROC_NOISY_FRAME_TIME
    log_debug("Scanout 2 [{:.3f}]", std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
    if (output->scanout_time.load() < output->acquire_time) {
        log_warn("Scanout from earlier frame, skipping..");
        return 0;
    }

    wroc_post_event(output->server, wroc_output_event {
        .type = wroc_event_type::output_frame,
        .output = output,
    });

    return 0;
}

static
void create_output(wroc_direct_backend* backend, const VkDisplayPropertiesKHR& display_props)
{
    auto* wren  =backend->server->renderer->wren.get();
    auto display = display_props.display;

    std::vector<VkDisplayModePropertiesKHR> modes;
    wren_vk_enumerate(modes, wren->vk.GetDisplayModePropertiesKHR, wren->physical_device, display);
    if (modes.empty()) {
        log_error("Cannot find any mode for display");
        return;
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
        return;
    }

    u32 plane_index = ~0u;

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
                break;
            }
        }
    }

    if (plane_index == ~0u) {
        log_error("Could not find plane");
        return;
    }

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

    output->vk_display = display;

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

    output->eventfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    output->scanout = wl_event_loop_add_fd(wl_display_get_event_loop(backend->server->display), output->eventfd, WL_EVENT_READABLE, handle_display_scanout, output.get());

    output->scanout_thread = std::jthread{[wren = wren, output = output.get()](std::stop_token stop) {
        scanout_thread(stop, wren, output);
    }};
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

    // auto display_props = displays.front();
    // auto display = display_props.display;
    // log_info("Selecting display: {}", display_props.displayName);

    for (auto& display : displays) {
        create_output(backend, display);
    }
}

wroc_drm_output::~wroc_drm_output()
{
    close(eventfd);
    wl_event_source_remove(scanout);
    scanout_thread.get_stop_source().request_stop();
}

void wroc_direct_backend::create_output()
{
    log_error("DRM backend does not support creating new outputs");
}

void wroc_direct_backend::destroy_output(wroc_output* output)
{
    wroc_post_event(output->server, wroc_output_event {
        .type = wroc_event_type::output_removed,
        .output = output,
    });

    std::erase_if(outputs, [&](auto& o) { return o.get() == output; });
}
