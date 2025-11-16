#include "wren_internal.hpp"

#include "wroc/server.hpp"

wrei_ref<wren_context> wren_create()
{
    auto ctx = wrei_adopt_ref(new wren_context {});

    ctx->vulkan1 = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!ctx->vulkan1) {
        log_error("Failed to load vulkan library");
        return nullptr;
    }

    dlerror();

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
    };

    wren_check(ctx->vk.CreateInstance(wrei_ptr_to(VkInstanceCreateInfo {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = wrei_ptr_to(VkApplicationInfo {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .apiVersion = VK_API_VERSION_1_3,
        }),
        .enabledExtensionCount = u32(instance_extensions.size()),
        .ppEnabledExtensionNames = instance_extensions.data(),
    }), nullptr, &ctx->instance));

    wren_load_instance_functions(ctx.get());

    std::vector<VkPhysicalDevice> physical_devices;
    wren_vk_enumerate(physical_devices, ctx->vk.EnumeratePhysicalDevices, ctx->instance);
    for (u32 i = 0; i < physical_devices.size(); ++i) {
        VkPhysicalDeviceProperties2 props { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        ctx->vk.GetPhysicalDeviceProperties2(physical_devices[i], &props);

        log_info("Device: {}", props.properties.deviceName);
    }

    if (physical_devices.empty()) {
        log_error("no vulkan capable devices found");
        return nullptr;
    }

    {
        ctx->physical_device = physical_devices[0];

        VkPhysicalDeviceProperties2 props { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        ctx->vk.GetPhysicalDeviceProperties2(ctx->physical_device, &props);
        log_info("  Selected: {}", props.properties.deviceName);
    }

    ctx->queue_family = ~0u;
    std::vector<VkQueueFamilyProperties> queue_props;
    wren_vk_enumerate(queue_props, ctx->vk.GetPhysicalDeviceQueueFamilyProperties, ctx->physical_device);
    for (u32 i = 0; i < queue_props.size(); ++i) {
        if (queue_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            ctx->queue_family = i;
            break;
        }
    }

    std::array device_extensions {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
        VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,
        VK_KHR_MAINTENANCE_5_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
        VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
        VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME,
    };

    wren_check(ctx->vk.CreateDevice(ctx->physical_device, wrei_ptr_to(VkDeviceCreateInfo {
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
            }),
            wrei_ptr_to(VkPhysicalDeviceMaintenance5Features {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES,
                .maintenance5 = true,
            }),
            wrei_ptr_to(VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT,
                .swapchainMaintenance1 = true,
            }),
        }),
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = wrei_ptr_to(VkDeviceQueueCreateInfo {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = ctx->queue_family,
            .queueCount = 1,
            .pQueuePriorities = wrei_ptr_to(1.f),
        }),
        .enabledExtensionCount = u32(device_extensions.size()),
        .ppEnabledExtensionNames = device_extensions.data(),
    }), nullptr, &ctx->device));

    wren_load_device_functions(ctx.get());

    ctx->vk.GetDeviceQueue(ctx->device, ctx->queue_family, 0, &ctx->queue);

    wren_check(vkwsi_context_create(&ctx->vkwsi, wrei_ptr_to(vkwsi_context_info{
        .instance = ctx->instance,
        .device = ctx->device,
        .physical_device = ctx->physical_device,
        .get_instance_proc_addr = ctx->vk.GetInstanceProcAddr,
        .log_callback = {
            .fn = [](void*, vkwsi_log_level level, const char* message) -> void {
                switch (level) {
                    break;case vkwsi_log_level_error:  wrei_log(wrei_log_level::error, message);
                    break;case vkwsi_log_level_warn:   wrei_log(wrei_log_level::warn, message);
                    break;case vkwsi_log_level_info:   wrei_log(wrei_log_level::info, message);
                    break;case vkwsi_log_level_trace:  wrei_log(wrei_log_level::trace, message);
                }
            }
        },
    })));

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

    wren_check(ctx->vk.CreateCommandPool(ctx->device, wrei_ptr_to(VkCommandPoolCreateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = ctx->queue_family,
    }), nullptr, &ctx->cmd_pool));

    wren_init_descriptors(ctx.get());

    return ctx;
}
