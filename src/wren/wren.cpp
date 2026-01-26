#include "internal.hpp"

#include "wrei/types.hpp"
#include "wrei/util.hpp"

#include <sys/sysmacros.h>

const char* wren_result_to_string(VkResult res)
{
    return string_VkResult(res);
}

wren_context::~wren_context()
{
    log_info("Wren context destroyed");

    graphics_queue = nullptr;
    transfer_queue = nullptr;

    wren_destroy_gbm_allocator(this);

    assert(stats.active_images == 0);
    assert(stats.active_buffers == 0);
    assert(stats.active_samplers == 0);

    vmaDestroyAllocator(vma);

    for (auto* binary_sema : free_binary_semaphores) {
        vk.DestroySemaphore(device, binary_sema, nullptr);
    }

    vk.DestroyPipelineLayout(device, pipeline_layout, nullptr);
    vk.DestroyDescriptorSetLayout(device, set_layout, nullptr);
    vk.DestroyDescriptorPool(device, pool, nullptr);

    vk.DestroyDevice(device, nullptr);
    vk.DestroyInstance(instance, nullptr);
}

static
void drop_capabilities()
{
    cap_t caps = cap_init();
    cap_set_proc(caps);
    cap_free(caps);
}

ref<wren_context> wren_create(wren_features _features, wrei_event_loop* event_loop)
{
    auto ctx = wrei_create<wren_context>();
    ctx->features = _features;

    ctx->event_loop = event_loop;

    // Loader

    ctx->vulkan1 = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!ctx->vulkan1) {
        log_error("Failed to load vulkan library");
        return nullptr;
    }

    dlerror();

    // Instance

    auto vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(ctx->vulkan1, "vkGetInstanceProcAddr"));
    if (!vkGetInstanceProcAddr) {
        log_error("Failed to load vulkan library");
        return nullptr;
    }

    wren_init_functions(ctx.get(), vkGetInstanceProcAddr);

    std::vector<const char*> instance_extensions {
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME,
        VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME,
        VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
        VK_KHR_DISPLAY_EXTENSION_NAME,
        VK_EXT_DISPLAY_SURFACE_COUNTER_EXTENSION_NAME,
    };

    wren_check(ctx->vk.CreateInstance(wrei_ptr_to(VkInstanceCreateInfo {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = wrei_ptr_to(VkApplicationInfo {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .apiVersion = VK_API_VERSION_1_4,
        }),
        .enabledExtensionCount = u32(instance_extensions.size()),
        .ppEnabledExtensionNames = instance_extensions.data(),
    }), nullptr, &ctx->instance));

    wren_load_instance_functions(ctx.get());

    // Select GPU

    std::vector<VkPhysicalDevice> physical_devices;
    wren_vk_enumerate(physical_devices, ctx->vk.EnumeratePhysicalDevices, ctx->instance);
    for (u32 i = 0; i < physical_devices.size(); ++i) {
        VkPhysicalDeviceProperties2 props { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        ctx->vk.GetPhysicalDeviceProperties2(physical_devices[i], &props);

        log_info("Device: {}", props.properties.deviceName);
    }

    if (physical_devices.empty()) {
        log_error("No vulkan capable devices found");
        return nullptr;
    }

    {
        ctx->physical_device = physical_devices[0];

        VkPhysicalDeviceProperties2 props { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        ctx->vk.GetPhysicalDeviceProperties2(ctx->physical_device, &props);
        log_info("  Selected: {}", props.properties.deviceName);
    }

    // Device extension support

    std::vector device_extensions {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
        VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,
        VK_KHR_MAINTENANCE_5_EXTENSION_NAME,
        VK_EXT_DISPLAY_CONTROL_EXTENSION_NAME,
        VK_KHR_GLOBAL_PRIORITY_EXTENSION_NAME,
        VK_KHR_UNIFIED_IMAGE_LAYOUTS_EXTENSION_NAME,

        VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
        VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
        VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
        VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME,
        VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME,
    };

    {
        // DRM file descriptor

        VkPhysicalDeviceDrmPropertiesEXT drm_props {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT,
        };
        ctx->vk.GetPhysicalDeviceProperties2(ctx->physical_device, wrei_ptr_to(VkPhysicalDeviceProperties2 {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
            .pNext = &drm_props,
        }));

        if (drm_props.hasRender) {
            ctx->dev_id = makedev(drm_props.renderMajor, drm_props.renderMinor);
        } else if (drm_props.hasPrimary) {
            ctx->dev_id = makedev(drm_props.primaryMajor, drm_props.primaryMinor);
        } else {
            log_error("Vulkan physical has no render or primary nodes");
            return nullptr;
        }

        log_info("Device ID: {}", ctx->dev_id);

        // Get DRM fd

        drmDevice* device = nullptr;
        wrei_unix_check_ne(drmGetDeviceFromDevId(ctx->dev_id, 0, &device));
        defer { drmFreeDevice(&device); };
        const char* name = nullptr;
        if (device->available_nodes & (1 << DRM_NODE_RENDER)) {
            name = device->nodes[DRM_NODE_RENDER];
        } else {
            assert(device->available_nodes & (1 << DRM_NODE_PRIMARY));
            name = device->nodes[DRM_NODE_PRIMARY];
            log_debug("DRM device {} has no render node, falling back to primary node", name);
        }
        log_info("Device path: {}", name);
        ctx->drm_fd = open(name, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    }

    std::vector<VkExtensionProperties> available_extensions;
    {
        u32 count = 0;
        wren_check(ctx->vk.EnumerateDeviceExtensionProperties(ctx->physical_device, nullptr, &count, nullptr));
        available_extensions.resize(count);
        wren_check(ctx->vk.EnumerateDeviceExtensionProperties(ctx->physical_device, nullptr, &count, available_extensions.data()));
    }

    auto check_extension = [&](const char* name) -> bool {
        for (auto& extension : available_extensions) {
            if (strcmp(extension.extensionName, name) == 0) {
                return true;
            }
        }
        return false;
    };

    for (auto& ext : device_extensions) {
        if (!check_extension(ext)) {
            log_error("Extension not present: {}", ext);
            wrei_debugkill();
        }
    }

    // Device creation

    static constexpr u32 invalid_index = ~0u;
    u32 graphics_queue_family = invalid_index;
    u32 transfer_queue_family = invalid_index;

    std::vector<VkQueueFamilyProperties> queue_props;
    wren_vk_enumerate(queue_props, ctx->vk.GetPhysicalDeviceQueueFamilyProperties, ctx->physical_device);
    for (u32 i = 0; i < queue_props.size(); ++i) {
        if (queue_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            if (graphics_queue_family == invalid_index) {
                graphics_queue_family = i;
            }
        } else if (queue_props[i].queueFlags & VK_QUEUE_TRANSFER_BIT) {
            if (transfer_queue_family == invalid_index) {
                transfer_queue_family = i;
            }
        }
    }

    auto create_device = [&](bool global_priority) {
        return wren_check(ctx->vk.CreateDevice(ctx->physical_device, wrei_ptr_to(VkDeviceCreateInfo {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = wren_vk_make_chain_in({
                wrei_ptr_to(VkPhysicalDeviceFeatures2 {
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                    .features = {
                        .shaderInt64 = true,
                        .shaderInt16 = true,
                    },
                }),
                wrei_ptr_to(VkPhysicalDeviceVulkan11Features {
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
                    .storagePushConstant16 = true,
                    .samplerYcbcrConversion = true,
                    .shaderDrawParameters = true,
                }),
                wrei_ptr_to(VkPhysicalDeviceVulkan12Features {
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
                    .storagePushConstant8 = true,
                    .shaderInt8 = true,
                    .descriptorIndexing = true,
                    .shaderSampledImageArrayNonUniformIndexing = true,
                    .descriptorBindingSampledImageUpdateAfterBind = true,
                    .descriptorBindingUpdateUnusedWhilePending = true,
                    .descriptorBindingPartiallyBound = true,
                    .runtimeDescriptorArray = true,
                    .scalarBlockLayout = true,
                    .timelineSemaphore = true,
                    .bufferDeviceAddress = true,
                }),
                wrei_ptr_to(VkPhysicalDeviceVulkan13Features {
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
                    .synchronization2 = true,
                    .dynamicRendering = true,
                    .maintenance4 = true,
                }),
                wrei_ptr_to(VkPhysicalDeviceVulkan14Features {
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
                    .maintenance5 = true,
                    .maintenance6 = true,
                }),
                wrei_ptr_to(VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR {
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT,
                    .swapchainMaintenance1 = true,
                }),
                wrei_ptr_to(VkPhysicalDeviceUnifiedImageLayoutsFeaturesKHR {
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFIED_IMAGE_LAYOUTS_FEATURES_KHR,
                    .unifiedImageLayouts = true,
                    .unifiedImageLayoutsVideo = true,
                }),
            }),
            .queueCreateInfoCount = 2,
            .pQueueCreateInfos = std::array {
                VkDeviceQueueCreateInfo {
                    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                    .pNext =
                        global_priority
                            ? wrei_ptr_to(VkDeviceQueueGlobalPriorityCreateInfoKHR {
                                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO,
                                .globalPriority = VK_QUEUE_GLOBAL_PRIORITY_HIGH,
                            })
                            : nullptr,
                    .queueFamilyIndex = graphics_queue_family,
                    .queueCount = 1,
                    .pQueuePriorities = wrei_ptr_to(1.f),
                },
                VkDeviceQueueCreateInfo {
                    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                    .queueFamilyIndex = transfer_queue_family,
                    .queueCount = 1,
                    .pQueuePriorities = wrei_ptr_to(1.f),
                },
            }.data(),
            .enabledExtensionCount = u32(device_extensions.size()),
            .ppEnabledExtensionNames = device_extensions.data(),
        }), nullptr, &ctx->device), VK_ERROR_NOT_PERMITTED);
    };

    if (create_device(true) == VK_ERROR_NOT_PERMITTED) {
        log_warn("Failed to acquire global queue priority, falling back to normal queue priorities");
        create_device(false);
    }

    // Drop CAP_SYS_NICE that was used to acquire global queue priority
    //
    // TODO: As is this prevents us from recreating the device on failure.
    //       We should keep capabilities and instead run a separate daemon for
    //       spawning subprocesses
    drop_capabilities();

    wren_load_device_functions(ctx.get());

    ctx->graphics_queue = wren_queue_init(ctx.get(), wren_queue_type::graphics, graphics_queue_family);
    ctx->transfer_queue = wren_queue_init(ctx.get(), wren_queue_type::transfer, transfer_queue_family);

    // VMA allocator

    wren_check(vmaCreateAllocator(wrei_ptr_to(VmaAllocatorCreateInfo {
        .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice = ctx->physical_device,
        .device = ctx->device,
        .pVulkanFunctions = wrei_ptr_to(VmaVulkanFunctions {
            .vkGetInstanceProcAddr = ctx->vk.GetInstanceProcAddr,
            .vkGetDeviceProcAddr = ctx->vk.GetDeviceProcAddr,
        }),
        .instance = ctx->instance,
        .vulkanApiVersion = VK_API_VERSION_1_3,
    }), &ctx->vma));

    // GBM allocator

    wren_init_gbm_allocator(ctx.get());

    // State initialization

    wren_init_descriptors(ctx.get());

    wren_register_formats(ctx.get());

    log_info("shm texture formats: {}", ctx->shm_texture_formats.size());
    log_info("render formats: {}", ctx->dmabuf_render_formats.size());
    log_info("dmabuf texture formats: {}", ctx->dmabuf_texture_formats.size());

    return ctx;
}
