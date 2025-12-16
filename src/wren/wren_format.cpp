#include "wren/wren_internal.hpp"

#include "wrei/util.hpp"

static constexpr wren_format formats[] {
	{
		.drm = DRM_FORMAT_XRGB8888,
		.vk = VK_FORMAT_B8G8R8A8_UNORM,
		.vk_srgb = VK_FORMAT_B8G8R8A8_SRGB,
	},

	// The Vulkan _SRGB formats correspond to unpremultiplied alpha, but
	// the Wayland protocol specifies premultiplied alpha on electrical values
	{
		.drm = DRM_FORMAT_ARGB8888,
		.vk = VK_FORMAT_B8G8R8A8_UNORM,
	},

};

std::span<const wren_format> wren_get_formats()
{
    return formats;
}

std::optional<wren_format> wren_find_format_from_vulkan(VkFormat vk_format)
{
    for (auto& format : formats) {
        if (format.vk == vk_format || format.vk_srgb == vk_format) {
            return format;
        }
    }

    return std::nullopt;
}

std::optional<wren_format> wren_find_format_from_drm(u32 drm_format)
{
    for (auto& format : formats) {
        if (format.drm == drm_format) {
            return format;
        }
    }

    return std::nullopt;
}

void wren_enumerate_drm_modifiers(wren_context* ctx, const wren_format& format, std::vector<VkDrmFormatModifierProperties2EXT>& modifiers)
{
    VkDrmFormatModifierPropertiesList2EXT mod_list = {
        .sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_2_EXT,
    };
    VkFormatProperties2 props = {
        .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
        .pNext = &mod_list,
    };
    ctx->vk.GetPhysicalDeviceFormatProperties2(ctx->physical_device, format.vk, &props);

    log_info("Modifier count: {}", mod_list.drmFormatModifierCount);
    modifiers.resize(mod_list.drmFormatModifierCount);

    mod_list.pDrmFormatModifierProperties = modifiers.data();

    ctx->vk.GetPhysicalDeviceFormatProperties2(ctx->physical_device, format.vk, &props);
}

ref<wren_image> wren_image_import_dmabuf(wren_context* ctx, const wren_dma_params& params)
{
    auto image = wrei_adopt_ref(wrei_get_registry(ctx)->create<wren_image>());
    image->ctx = ctx;

    image->extent = params.extent;

    VkExternalMemoryHandleTypeFlagBits htype = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    VkImageCreateInfo img_info {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = params.format.vk,
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .extent = {image->extent.x, image->extent.y, 1},
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
    };

    VkExternalMemoryImageCreateInfo eimg = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
    };
    img_info.pNext = &eimg;

    VkSubresourceLayout plane_layouts[wren_dma_max_planes] = {};

    img_info.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
    for (u32 i = 0; i < params.planes.size(); ++i) {
        plane_layouts[i].offset = params.planes[i].offset;
        plane_layouts[i].rowPitch = params.planes[i].stride;
    }

    VkImageDrmFormatModifierExplicitCreateInfoEXT mod_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT,
        .drmFormatModifierPlaneCount = u32(params.planes.size()),
        .drmFormatModifier = params.planes.front().drm_modifier,
        .pPlaneLayouts = plane_layouts,
    };
    eimg.pNext = &mod_info;

    wren_check(ctx->vk.CreateImage(ctx->device, &img_info, nullptr, &image->image));

    image->dma_params = std::make_unique<wren_dma_params>(params);

    VkBindImageMemoryInfo bindi = {};

    {
        VkMemoryFdPropertiesKHR fdp = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR,
        };
        log_trace("  num_planes = {}", params.planes.size());
        log_trace("  plane[0].fd = {}", params.planes.front().fd);
        log_trace("  ctx->vk.GetMemoryFdPropertiesKHR = {}", (void*)ctx->vk.GetMemoryFdPropertiesKHR);
        wren_check(ctx->vk.GetMemoryFdPropertiesKHR(ctx->device, htype, params.planes.front().fd, &fdp));

        // TODO: Multi-plane support
        assert(params.planes.size() == 1);

        VkImageMemoryRequirementsInfo2 memri = {
            .image = image->image,
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
        };

        VkMemoryRequirements2 memr = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
        };

        ctx->vk.GetImageMemoryRequirements2(ctx->device, &memri, &memr);

        auto mem = wren_find_vk_memory_type_index(ctx, memr.memoryRequirements.memoryTypeBits & fdp.memoryTypeBits, 0);

        // Take a copy of the file descriptor, this will be owned by the bound vulkan memory
        int dfd = fcntl(params.planes.front().fd, F_DUPFD_CLOEXEC, 0);
        image->dma_params->planes[0].fd = dfd;

        VkMemoryAllocateInfo memi = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = memr.memoryRequirements.size,
            .memoryTypeIndex = mem,
        };

        VkImportMemoryFdInfoKHR importi = {
            .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
            .fd = dfd,
            .handleType = htype,
        };
        memi.pNext = &importi;

        VkMemoryDedicatedAllocateInfo dedi = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
            .image = image->image,
        };
        importi.pNext = &dedi;

        wren_check(ctx->vk.AllocateMemory(ctx->device, &memi, nullptr, &image->memory));

        bindi.image = image->image;
        bindi.memory = image->memory;
        bindi.memoryOffset = 0;
        bindi.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
    }

    wren_check(ctx->vk.BindImageMemory2(ctx->device, 1, &bindi));

    {
        auto cmd = wren_begin_commands(ctx);

        wren_transition(ctx, cmd, image->image,
            0, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            0, VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

        wren_submit_commands(ctx, cmd);
    }

    wren_check(ctx->vk.CreateImageView(ctx->device, wrei_ptr_to(VkImageViewCreateInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = params.format.vk,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    }), nullptr, &image->view));

    wren_allocate_image_descriptor(image.get());

    return image;
}
