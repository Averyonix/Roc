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

    wrei_assert(waiter->waits.empty());
    close(waiter->fd);
    waiter->mark_defunct();

    graphics_queue = nullptr;
    transfer_queue = nullptr;

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

static
std::array required_device_extensions = {
    VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
    VK_KHR_MAINTENANCE_5_EXTENSION_NAME,
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

static
bool try_physical_device(wren_context* ctx, VkPhysicalDevice phdev, struct stat* drm_stat)
{
    {
        VkPhysicalDeviceProperties2 props { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        ctx->vk.GetPhysicalDeviceProperties2(phdev, &props);
        log_debug("Testing physical device: {}", props.properties.deviceName);
    }

    // Device extension support

    std::vector<VkExtensionProperties> available_extensions;
    {
        u32 count = 0;
        wren_check(ctx->vk.EnumerateDeviceExtensionProperties(phdev, nullptr, &count, nullptr));
        available_extensions.resize(count);
        wren_check(ctx->vk.EnumerateDeviceExtensionProperties(phdev, nullptr, &count, available_extensions.data()));
    }

    auto check_extension = [&](const char* name) -> bool {
        for (auto& extension : available_extensions) {
            if (strcmp(extension.extensionName, name) == 0) {
                return true;
            }
        }
        return false;
    };

    for (auto& ext : required_device_extensions) {
        if (!check_extension(ext)) {
            log_warn("  device mission extension: {}", ext);
            return false;
        }
    }

    // DRM file descriptor

    VkPhysicalDeviceDrmPropertiesEXT drm_props {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT,
    };
    ctx->vk.GetPhysicalDeviceProperties2(phdev, wrei_ptr_to(VkPhysicalDeviceProperties2 {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &drm_props,
    }));

    dev_t primary_dev_id = makedev(drm_props.primaryMajor, drm_props.primaryMinor);
    dev_t render_dev_id = makedev(drm_props.renderMajor, drm_props.renderMinor);

    if (drm_stat) {
        if (drm_props.hasPrimary && primary_dev_id == drm_stat->st_rdev) {
            log_debug("  matched primary device id");
        } else if (drm_props.hasRender && render_dev_id == drm_stat->st_rdev) {
            log_debug("  matched secondary device id");
        } else {
            log_warn("  device does not matched requested drm fd");
            return false;
        }
    } else {
        if (!drm_props.hasPrimary && !drm_props.hasRender) {
            log_warn("  device has no primary or render node");
            return false;
        }
    }

    // Prefer to open the render node for normal render operations,
    //   even the requested drm was opened from a primary node

    ctx->dev_id = drm_props.hasRender ? render_dev_id : primary_dev_id;
    log_debug("  device id: {} ({})", ctx->dev_id, drm_props.hasRender ? "render" : "primary");

    // Open

    drmDevice* device = nullptr;
    unix_check(drmGetDeviceFromDevId(ctx->dev_id, 0, &device));
    defer { drmFreeDevice(&device); };
    const char* name = nullptr;

    if (device->available_nodes & (1 << DRM_NODE_RENDER)) {
        name = device->nodes[DRM_NODE_RENDER];
    } else {
        assert(device->available_nodes & (1 << DRM_NODE_PRIMARY));
        name = device->nodes[DRM_NODE_PRIMARY];
        log_warn("  device {} has no render node, falling back to primary node", name);
    }

    log_debug("  device path: {}", name);
    ctx->drm_fd = open(name, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    log_debug("  drm fd: {}", ctx->drm_fd);

    log_info("  device selected");

    return true;
}

ref<wren_context> wren_create(wren_features _features, wrei_event_loop* event_loop, int drm_fd)
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

    {
        struct stat drm_stat;
        if (drm_fd >= 0) {
            if (unix_check(fstat(drm_fd, &drm_stat)).err()) {
                log_error("Wren initialization failed - failed to fstat requested drm fd");
                return nullptr;
            }
            log_debug("Matching against requested DRM {} (id: {})", drm_fd, drm_stat.st_rdev);
        }
        for (auto& phdev : physical_devices) {
            if (try_physical_device(ctx.get(), phdev, drm_fd >= 0 ? &drm_stat : nullptr)) {
                ctx->physical_device = phdev;
                break;
            }
        }
    }

    if (!ctx->physical_device) {
        log_error("No suitable vulkan device found");
        return nullptr;
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
            .enabledExtensionCount = u32(required_device_extensions.size()),
            .ppEnabledExtensionNames = required_device_extensions.data(),
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
        .vulkanApiVersion = VK_API_VERSION_1_4,
    }), &ctx->vma));

    // State initialization

    wren_init_descriptors(ctx.get());

    // Semaphore waiter

    ctx->waiter = wrei_create<wren_semaphore_waiter>();
    ctx->waiter->fd = unix_check(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)).value;
    wrei_event_loop_add(ctx->event_loop.get(), EPOLLIN, ctx->waiter.get());

    return ctx;
}
