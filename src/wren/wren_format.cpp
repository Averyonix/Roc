#include "wren/wren_internal.hpp"

#include "wrei/util.hpp"

// We only support little endian architectures
static_assert(std::endian::native == std::endian::little);

struct wren_format_t_create_params
{
    u32 drm;
    VkFormat vk;
    VkFormat vk_srgb;
    bool is_ycbcr;
};

#define WROC_FORMAT wren_format_t_create_params

constexpr wren_format_t::wren_format_t(const wren_format_t_create_params& params)
    : drm(params.drm)
    , vk(params.vk)
    , vk_srgb(params.vk_srgb)
    , is_ycbcr(params.is_ycbcr)
{
    switch (drm) {
        break;case DRM_FORMAT_XRGB8888:
            shm = WL_SHM_FORMAT_XRGB8888;
        break;case DRM_FORMAT_ARGB8888:
            shm = WL_SHM_FORMAT_ARGB8888;
        break;default:
            shm = wl_shm_format(drm);
    }
}

// format list courtesy of wlroots pixel_format.c
static constexpr wren_format_t formats[] {

    // Vulkan non-packed 8-bits-per-channel formats have an inverted channel
    // order compared to the DRM formats, because DRM format channel order
    // is little-endian while Vulkan format channel order is in memory byte
    // order.
    WROC_FORMAT {
        .drm = DRM_FORMAT_R8,
        .vk = VK_FORMAT_R8_UNORM,
        .vk_srgb = VK_FORMAT_R8_SRGB,
    },
    WROC_FORMAT {
        .drm = DRM_FORMAT_GR88,
        .vk = VK_FORMAT_R8G8_UNORM,
        .vk_srgb = VK_FORMAT_R8G8_SRGB,
    },
    WROC_FORMAT {
        .drm = DRM_FORMAT_RGB888,
        .vk = VK_FORMAT_B8G8R8_UNORM,
        .vk_srgb = VK_FORMAT_B8G8R8_SRGB,
    },
    WROC_FORMAT {
        .drm = DRM_FORMAT_BGR888,
        .vk = VK_FORMAT_R8G8B8_UNORM,
        .vk_srgb = VK_FORMAT_R8G8B8_SRGB,
    },
    WROC_FORMAT {
        .drm = DRM_FORMAT_XRGB8888,
        .vk = VK_FORMAT_B8G8R8A8_UNORM,
        .vk_srgb = VK_FORMAT_B8G8R8A8_SRGB,
    },
    WROC_FORMAT {
        .drm = DRM_FORMAT_XBGR8888,
        .vk = VK_FORMAT_R8G8B8A8_UNORM,
        .vk_srgb = VK_FORMAT_R8G8B8A8_SRGB,
    },

    // The Vulkan _SRGB formats correspond to unpremultiplied alpha, but
    // the Wayland protocol specifies premultiplied alpha on electrical values
    WROC_FORMAT {
        .drm = DRM_FORMAT_ARGB8888,
        .vk = VK_FORMAT_B8G8R8A8_UNORM,
    },
    WROC_FORMAT {
        .drm = DRM_FORMAT_ABGR8888,
        .vk = VK_FORMAT_R8G8B8A8_UNORM,
    },

    // Vulkan packed formats have the same channel order as DRM formats on
    // little endian systems.
    WROC_FORMAT {
        .drm = DRM_FORMAT_RGBA4444,
        .vk = VK_FORMAT_R4G4B4A4_UNORM_PACK16,
    },
    WROC_FORMAT {
        .drm = DRM_FORMAT_RGBX4444,
        .vk = VK_FORMAT_R4G4B4A4_UNORM_PACK16,
    },
    WROC_FORMAT {
        .drm = DRM_FORMAT_BGRA4444,
        .vk = VK_FORMAT_B4G4R4A4_UNORM_PACK16,
    },
    WROC_FORMAT {
        .drm = DRM_FORMAT_BGRX4444,
        .vk = VK_FORMAT_B4G4R4A4_UNORM_PACK16,
    },
    WROC_FORMAT {
        .drm = DRM_FORMAT_RGB565,
        .vk = VK_FORMAT_R5G6B5_UNORM_PACK16,
    },
    WROC_FORMAT {
        .drm = DRM_FORMAT_BGR565,
        .vk = VK_FORMAT_B5G6R5_UNORM_PACK16,
    },
    WROC_FORMAT {
        .drm = DRM_FORMAT_RGBA5551,
        .vk = VK_FORMAT_R5G5B5A1_UNORM_PACK16,
    },
    WROC_FORMAT {
        .drm = DRM_FORMAT_RGBX5551,
        .vk = VK_FORMAT_R5G5B5A1_UNORM_PACK16,
    },
    WROC_FORMAT {
        .drm = DRM_FORMAT_BGRA5551,
        .vk = VK_FORMAT_B5G5R5A1_UNORM_PACK16,
    },
    WROC_FORMAT {
        .drm = DRM_FORMAT_BGRX5551,
        .vk = VK_FORMAT_B5G5R5A1_UNORM_PACK16,
    },
    WROC_FORMAT {
        .drm = DRM_FORMAT_ARGB1555,
        .vk = VK_FORMAT_A1R5G5B5_UNORM_PACK16,
    },
    WROC_FORMAT {
        .drm = DRM_FORMAT_XRGB1555,
        .vk = VK_FORMAT_A1R5G5B5_UNORM_PACK16,
    },
    WROC_FORMAT {
        .drm = DRM_FORMAT_ARGB2101010,
        .vk = VK_FORMAT_A2R10G10B10_UNORM_PACK32,
    },
    WROC_FORMAT {
        .drm = DRM_FORMAT_XRGB2101010,
        .vk = VK_FORMAT_A2R10G10B10_UNORM_PACK32,
    },
    WROC_FORMAT {
        .drm = DRM_FORMAT_ABGR2101010,
        .vk = VK_FORMAT_A2B10G10R10_UNORM_PACK32,
    },
    WROC_FORMAT {
        .drm = DRM_FORMAT_XBGR2101010,
        .vk = VK_FORMAT_A2B10G10R10_UNORM_PACK32,
    },

    // Vulkan 16-bits-per-channel formats have an inverted channel order
    // compared to DRM formats, just like the 8-bits-per-channel ones.
    // On little endian systems the memory representation of each channel
    // matches the DRM formats'.
    WROC_FORMAT {
        .drm = DRM_FORMAT_ABGR16161616,
        .vk = VK_FORMAT_R16G16B16A16_UNORM,
    },
    WROC_FORMAT {
        .drm = DRM_FORMAT_XBGR16161616,
        .vk = VK_FORMAT_R16G16B16A16_UNORM,
    },
    WROC_FORMAT {
        .drm = DRM_FORMAT_ABGR16161616F,
        .vk = VK_FORMAT_R16G16B16A16_SFLOAT,
    },
    WROC_FORMAT {
        .drm = DRM_FORMAT_XBGR16161616F,
        .vk = VK_FORMAT_R16G16B16A16_SFLOAT,
    },

    // YCbCr formats
    // R -> V, G -> Y, B -> U
    // 420 -> 2x2 subsampled, 422 -> 2x1 subsampled, 444 -> non-subsampled
    WROC_FORMAT {
        .drm = DRM_FORMAT_UYVY,
        .vk = VK_FORMAT_B8G8R8G8_422_UNORM,
        .is_ycbcr = true,
    },
    WROC_FORMAT {
        .drm = DRM_FORMAT_YUYV,
        .vk = VK_FORMAT_G8B8G8R8_422_UNORM,
        .is_ycbcr = true,
    },
    WROC_FORMAT {
        .drm = DRM_FORMAT_NV12,
        .vk = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM,
        .is_ycbcr = true,
    },
    WROC_FORMAT {
        .drm = DRM_FORMAT_NV16,
        .vk = VK_FORMAT_G8_B8R8_2PLANE_422_UNORM,
        .is_ycbcr = true,
    },
    WROC_FORMAT {
        .drm = DRM_FORMAT_YUV420,
        .vk = VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM,
        .is_ycbcr = true,
    },
    WROC_FORMAT {
        .drm = DRM_FORMAT_YUV422,
        .vk = VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM,
        .is_ycbcr = true,
    },
    WROC_FORMAT {
        .drm = DRM_FORMAT_YUV444,
        .vk = VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM,
        .is_ycbcr = true,
    },

    // 3PACK16 formats split the memory in three 16-bit words, so they have an
    // inverted channel order compared to DRM formats.
    WROC_FORMAT {
        .drm = DRM_FORMAT_P010,
        .vk = VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16,
        .is_ycbcr = true,
    },
    WROC_FORMAT {
        .drm = DRM_FORMAT_P210,
        .vk = VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16,
        .is_ycbcr = true,
    },
    WROC_FORMAT {
        .drm = DRM_FORMAT_P012,
        .vk = VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16,
        .is_ycbcr = true,
    },
    WROC_FORMAT {
        .drm = DRM_FORMAT_P016,
        .vk = VK_FORMAT_G16_B16R16_2PLANE_420_UNORM,
        .is_ycbcr = true,
    },
    WROC_FORMAT {
        .drm = DRM_FORMAT_Q410,
        .vk = VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16,
        .is_ycbcr = true,
    },
};

#undef WROC_FORMAT

// -----------------------------------------------------------------------------

static constexpr VkFormatFeatureFlags wren_shm_texture_features
    = VK_FORMAT_FEATURE_TRANSFER_SRC_BIT
    | VK_FORMAT_FEATURE_TRANSFER_DST_BIT
    | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
    | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;

static constexpr VkImageUsageFlags wren_shm_texture_usage
    = VK_IMAGE_USAGE_SAMPLED_BIT
    | VK_IMAGE_USAGE_TRANSFER_DST_BIT
    | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

static constexpr VkFormatFeatureFlags wren_render_features
    = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT
    | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;

static constexpr VkImageUsageFlags wren_render_usage
    = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

static constexpr VkFormatFeatureFlags wren_dma_texture_features
    = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
    | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;

static constexpr VkImageUsageFlags wren_dma_texture_usage
    = VK_IMAGE_USAGE_SAMPLED_BIT
    | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

static constexpr VkFormatFeatureFlags wren_ycbcr_texture_features
    = VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT
    | VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT;

// -----------------------------------------------------------------------------

std::span<const wren_format_t> wren_get_formats()
{
    return formats;
}

template<typename T>
wren_format wren_find_format(T needle, auto... members)
{
    for (auto& format : formats) {
        if (((format.*members == needle) || ...)) return &format;
    }

    return nullptr;
}

wren_format wren_find_format_from_vulkan(VkFormat vk_format)
{
    return wren_find_format(vk_format, &wren_format_t::vk, &wren_format_t::vk_srgb);
}

wren_format wren_find_format_from_drm(u32 drm_format)
{
    return wren_find_format(drm_format, &wren_format_t::drm);
}

wren_format wren_find_format_from_shm(wl_shm_format shm_format)
{
    return wren_find_format(shm_format, &wren_format_t::shm);
}

static
bool wren_query_image_format_support(
    wren_context* ctx,
    VkFormat format, VkFormat srgb_format,
    VkImageUsageFlags usage,
    const VkDrmFormatModifierProperties2EXT* drm_props,
    bool* has_mutable_srgb, VkImageFormatProperties* out)
{
    bool has_srgb_format = srgb_format != VK_FORMAT_UNDEFINED;

    VkFormat view_formats[2] {
        format,
        srgb_format,
    };

    VkPhysicalDeviceExternalImageFormatInfo external_image_info;
    VkPhysicalDeviceImageDrmFormatModifierInfoEXT mod_info;
    VkExternalImageFormatProperties external_image_props;

    VkImageFormatListCreateInfo format_list {
        .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO,
        .viewFormatCount = has_srgb_format ? 2u : 1u,
        .pViewFormats = view_formats,
    };

    VkImageCreateFlags image_flags = {};

    VkPhysicalDeviceImageFormatInfo2 format_info {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
        .pNext = &format_list,
        .format = format,
        .type = VK_IMAGE_TYPE_2D,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .flags = image_flags | (has_srgb_format ? VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT : 0),
    };

    VkImageFormatProperties2 format_props {
        .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
    };

    if (drm_props) {
        format_info.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
        mod_info = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT,
            .pNext = &format_list,
            .drmFormatModifier = drm_props->drmFormatModifier,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        external_image_info = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
            .pNext = &mod_info,
            .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
        };
        format_info.pNext = &external_image_info;

        external_image_props = {
            .sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES,
        };
        format_props.pNext = &external_image_props;
    };

    auto check = [&] {
        auto res = wren_check(ctx->vk.GetPhysicalDeviceImageFormatProperties2(ctx->physical_device, &format_info, &format_props), VK_ERROR_FORMAT_NOT_SUPPORTED);
        if (res != VK_SUCCESS) {
            // Unsupported
            return false;
        }

        if (drm_props && !(external_image_props.externalMemoryProperties.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT)) {
            // Not importable
            return false;
        }

        *out = format_props.imageFormatProperties;

        return true;
    };

    if (check()) {
        *has_mutable_srgb = has_srgb_format;
        return true;
    }

    if (!has_srgb_format) return false;

    // Try without srgb format
    format_list.viewFormatCount = 1;
    format_info.flags = image_flags;
    return check();
}

static
bool wren_try_register_shm_format(wren_context* ctx, wren_format format, VkFormatProperties props, wren_format_props* out)
{
    if (format->is_ycbcr || (props.optimalTilingFeatures & wren_shm_texture_features) != wren_shm_texture_features) {
        return false;
    }

    VkImageFormatProperties image_props;
    bool has_mutable_srgb = false;
    bool supported = wren_query_image_format_support(ctx, format->vk, format->vk_srgb, wren_shm_texture_usage, nullptr, &has_mutable_srgb, &image_props);

    if (!supported) return false;

    out->shm.max_extent = {image_props.maxExtent.width, image_props.maxExtent.height};
    out->shm.features = props.optimalTilingFeatures;
    out->shm.has_mutable_srgb = has_mutable_srgb;

    ctx->shm_texture_formats.add(format, DRM_FORMAT_MOD_LINEAR);

    return true;
}

static
bool wren_try_register_dmabuf_format(wren_context* ctx, wren_format format, u32 mod_count, wren_format_props* out)
{
    if (mod_count == 0) return false;

    std::vector<VkDrmFormatModifierProperties2EXT> mod_props(mod_count);
    ctx->vk.GetPhysicalDeviceFormatProperties2(ctx->physical_device, format->vk, wrei_ptr_to(VkFormatProperties2 {
        .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
        .pNext = wrei_ptr_to(VkDrmFormatModifierPropertiesList2EXT {
            .sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_2_EXT,
            .drmFormatModifierCount = mod_count,
            .pDrmFormatModifierProperties = mod_props.data(),
        })
    }));

    for (const auto m : mod_props) {

        auto check = [&](VkFormatFeatureFlags2 features, VkImageUsageFlags usage) -> std::optional<wren_format_modifier_props> {
            if (format->is_ycbcr) {
                features |= wren_ycbcr_texture_features;
            }
            if ((m.drmFormatModifierTilingFeatures & features) != features) {
                return std::nullopt;
            }

            VkImageFormatProperties image_props;
            bool has_mutable_srgb = false;
            if (!wren_query_image_format_support(ctx, format->vk, format->vk_srgb, usage, &m, &has_mutable_srgb, &image_props)) {
                return std::nullopt;
            }

            return wren_format_modifier_props {
                .props = m,
                .max_extent = {image_props.maxExtent.width, image_props.maxExtent.height},
                .has_mutable_srgb = has_mutable_srgb,
            };
        };

        if (!out->format->is_ycbcr) {
            if (auto render_props = check(wren_render_features, wren_render_usage)) {
                out->dmabuf.render_mods.emplace_back(*render_props);
                ctx->dmabuf_render_formats.add(format, m.drmFormatModifier);
            }
        }

        if (auto texture_props = check(wren_dma_texture_features, wren_dma_texture_usage)) {
            out->dmabuf.texture_mods.emplace_back(*texture_props);
            ctx->dmabuf_texture_formats.add(format, m.drmFormatModifier);
        }
    }

    return !out->dmabuf.render_mods.empty() || !out->dmabuf.texture_mods.empty();
}

void wren_register_format(wren_context* ctx, wren_format format)
{
    VkDrmFormatModifierPropertiesList2EXT mod_list = {
        .sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_2_EXT,
    };
    VkFormatProperties2 props = {
        .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
        .pNext = &mod_list,
    };
    ctx->vk.GetPhysicalDeviceFormatProperties2(ctx->physical_device, format->vk, &props);

    wren_format_props out_props = {
        .format = format,
    };

    bool register_props = false;
    register_props |= wren_try_register_shm_format(ctx, format, props.formatProperties, &out_props);
    register_props |= wren_try_register_dmabuf_format(ctx, format, mod_list.drmFormatModifierCount, &out_props);

    if (wrei_is_log_level_enabled(wrei_log_level::debug)) {
        std::string supported;
        auto add = [&](std::string_view support) {
            if (!supported.empty()) supported += '|';
            supported += support;
        };
        if (out_props.shm.features) add("shared");
        if (!out_props.dmabuf.render_mods.empty()) add(std::format("render({})", out_props.dmabuf.render_mods.size()));
        if (!out_props.dmabuf.texture_mods.empty()) add(std::format("texture({})", out_props.dmabuf.texture_mods.size()));

        if (register_props) {
            log_debug("Format {:4}{} -> {}", drmGetFormatName(format->drm), format->is_ycbcr ? " (YCbCr)" : "", supported);
        }
    }

    if (register_props) {
        ctx->format_props.insert({ format, std::move(out_props) });
    }
}

const wren_format_props* wren_get_format_props(wren_context* ctx, wren_format format)
{
    auto iter = ctx->format_props.find(format);
    return iter != ctx->format_props.end() ? &iter->second : nullptr;
}

// -----------------------------------------------------------------------------

ref<wren_image> wren_image_import_dmabuf(wren_context* ctx, const wren_dma_params& params)
{
    auto image = wrei_adopt_ref(wrei_get_registry(ctx)->create<wren_image>());
    image->ctx = ctx;

    image->extent = params.extent;

    VkExternalMemoryHandleTypeFlagBits htype = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    VkImageCreateInfo img_info {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = params.format->vk,
        .extent = {image->extent.x, image->extent.y, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .usage = wren_dma_texture_usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
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
        .drmFormatModifier = params.planes.front().drm_modifier,
        .drmFormatModifierPlaneCount = u32(params.planes.size()),
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
        log_trace("  plane[0].modifier = {}", drmGetFormatModifierName(params.planes.front().drm_modifier));
        log_trace("  ctx->vk.GetMemoryFdPropertiesKHR = {}", (void*)ctx->vk.GetMemoryFdPropertiesKHR);
        wren_check(ctx->vk.GetMemoryFdPropertiesKHR(ctx->device, htype, params.planes.front().fd, &fdp));

        // TODO: Multi-plane support
        assert(params.planes.size() == 1);

        VkImageMemoryRequirementsInfo2 memri = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
            .image = image->image,
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
            .handleType = htype,
            .fd = dfd,
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
        .format = params.format->vk,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    }), nullptr, &image->view));

    wren_allocate_image_descriptor(image.get());

    return image;
}
