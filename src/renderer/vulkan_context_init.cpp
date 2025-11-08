#include "vulkan_context.hpp"
#include "vulkan_helpers.hpp"

#include "compositor/display.hpp"

#include <dlfcn.h>

VulkanContext* vulkan_context_create(Backend* backend)
{
    auto vk = new VulkanContext {};
    defer { vulkan_context_destroy(vk); };

    vk->vulkan1 = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!vk->vulkan1) {
        log_error("Failed to load vulkan library");
        return nullptr;
    }

    dlerror();

    auto vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(vk->vulkan1, "vkGetInstanceProcAddr"));
    if (!vkGetInstanceProcAddr) {
        log_error("Failed to load vulkan library");
        return nullptr;
    }

    vulkan_init_functions(vk, vkGetInstanceProcAddr);

    std::vector<const char*> instance_extensions {
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME,
        VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME,
    };
    instance_extensions.append_range(backend_get_required_instance_extensions(backend));

    vk_check(vk->CreateInstance(ptr_to(VkInstanceCreateInfo {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = ptr_to(VkApplicationInfo {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .apiVersion = VK_API_VERSION_1_3,
        }),
        .enabledExtensionCount = u32(instance_extensions.size()),
        .ppEnabledExtensionNames = instance_extensions.data(),
    }), nullptr, &vk->instance));

    vulkan_load_instance_functions(vk);

    std::vector<VkPhysicalDevice> physical_devices;
    vk_enumerate(physical_devices, vk->EnumeratePhysicalDevices, vk->instance);
    for (u32 i = 0; i < physical_devices.size(); ++i) {
        VkPhysicalDeviceProperties2 props { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        vk->GetPhysicalDeviceProperties2(physical_devices[i], &props);

        log_info("Device: {}", props.properties.deviceName);
    }

    if (physical_devices.empty()) {
        log_error("no vulkan capable devices found");
        return nullptr;
    }

    {
        vk->physical_device = physical_devices[1];

        VkPhysicalDeviceProperties2 props { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        vk->GetPhysicalDeviceProperties2(vk->physical_device, &props);
        log_info("  Selected: {}", props.properties.deviceName);
    }

    vk->queue_family = ~0u;
    std::vector<VkQueueFamilyProperties> queue_props;
    vk_enumerate(queue_props, vk->GetPhysicalDeviceQueueFamilyProperties, vk->physical_device);
    for (u32 i = 0; i < queue_props.size(); ++i) {
        if (queue_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            vk->queue_family = i;
            break;
        }
    }

    std::array device_extensions {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
        VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,
        VK_KHR_MAINTENANCE_5_EXTENSION_NAME,
    };

    vk_check(vk->CreateDevice(vk->physical_device, ptr_to(VkDeviceCreateInfo {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = vk_make_chain_in({
            ptr_to(VkPhysicalDeviceFeatures2 {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                .features = {
                    .shaderInt64 = true,
                    .shaderInt16 = true,
                },
            }),
            ptr_to(VkPhysicalDeviceVulkan11Features {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
                .storagePushConstant16 = true,
                .shaderDrawParameters = true,
            }),
            ptr_to(VkPhysicalDeviceVulkan12Features {
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
            ptr_to(VkPhysicalDeviceVulkan13Features {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
                .synchronization2 = true,
                .dynamicRendering = true,
            }),
            ptr_to(VkPhysicalDeviceMaintenance5Features {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES,
                .maintenance5 = true,
            }),
            ptr_to(VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT,
                .swapchainMaintenance1 = true,
            }),
        }),
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = ptr_to(VkDeviceQueueCreateInfo {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = vk->queue_family,
            .queueCount = 1,
            .pQueuePriorities = ptr_to(1.f),
        }),
        .enabledExtensionCount = uint32_t(device_extensions.size()),
        .ppEnabledExtensionNames = device_extensions.data(),
    }), nullptr, &vk->device));

    vulkan_load_device_functions(vk);

    vk->GetDeviceQueue(vk->device, vk->queue_family, 0, &vk->queue);

    vk_check(vkwsi_context_create(&vk->vkwsi, ptr_to(vkwsi_context_info{
        .instance = vk->instance,
        .device = vk->device,
        .physical_device = vk->physical_device,
        .get_instance_proc_addr = vk->GetInstanceProcAddr,
        .log_callback = {
            .fn = [](void*, vkwsi_log_level level, const char* message) -> void {
                switch (level) {
                    break;case vkwsi_log_level_error:  log(LogLevel::error, message);
                    break;case vkwsi_log_level_warn:   log(LogLevel::warn, message);
                    break;case vkwsi_log_level_info:   log(LogLevel::info, message);
                    break;case vkwsi_log_level_trace:  log(LogLevel::trace, message);
                }
            }
        },
    })));

    vk_check(vk->CreateCommandPool(vk->device, ptr_to(VkCommandPoolCreateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = vk->queue_family,
    }), nullptr, &vk->cmd_pool));

    return std::exchange(vk, nullptr);
}
