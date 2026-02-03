#include "wren/internal.hpp"

#include "wrei/util.hpp"

//
// Formats are taken from wlroots
// To simply things we only support little endian architectures
//

static constexpr wren_drm_format wren_opaque_drm_formats[] {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_RGBX8888,
	DRM_FORMAT_BGRX8888,
	DRM_FORMAT_R8,
	DRM_FORMAT_GR88,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_RGBX4444,
	DRM_FORMAT_BGRX4444,
	DRM_FORMAT_RGBX5551,
	DRM_FORMAT_BGRX5551,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_XBGR2101010,
	DRM_FORMAT_XBGR16161616F,
	DRM_FORMAT_XBGR16161616,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_VYUY,
	DRM_FORMAT_NV12,
	DRM_FORMAT_P010,
};

struct wren_format_t_create_params
{
    wren_drm_format drm;
    VkFormat vk;
    VkFormat vk_srgb;
    bool is_ycbcr;
};

constexpr wren_format_t::wren_format_t(const wren_format_t_create_params& params)
    : name(drmGetFormatName(params.drm))
    , drm(params.drm)
    , vk(params.vk)
    , vk_srgb(params.vk_srgb)
    , is_ycbcr(params.is_ycbcr)
    , has_alpha(!std::ranges::contains(wren_opaque_drm_formats, params.drm))
{
    switch (drm) {
        break;case DRM_FORMAT_XRGB8888: shm = WL_SHM_FORMAT_XRGB8888;
        break;case DRM_FORMAT_ARGB8888: shm = WL_SHM_FORMAT_ARGB8888;
        break;default:                  shm = wl_shm_format(drm);
    }
}

#define WROC_FORMAT wren_format_t_create_params

const wren_format_t wren_formats[] {

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

std::span<const wren_format_t> wren_get_formats()
{
    return wren_formats;
}

// -----------------------------------------------------------------------------

template<typename T>
wren_format wren_find_format(T needle, auto... members)
{
    for (auto& format : wren_formats) {
        if (((format.*members == needle) || ...)) return &format;
    }

    return nullptr;
}

wren_format wren_format_from_drm(wren_drm_format drm_format)
{
    return wren_find_format(drm_format, &wren_format_t::drm);
}

wren_format wren_format_from_shm(wl_shm_format shm_format)
{
    return wren_find_format(shm_format, &wren_format_t::shm);
}

static
bool wren_query_image_format_support(
    wren_context* ctx,
    VkFormat format, VkFormat srgb_format,
    VkImageUsageFlags usage,
    const VkDrmFormatModifierProperties2EXT* drm_props,
    bool* has_mutable_srgb,
    VkImageFormatProperties* out,
    VkExternalMemoryProperties* out_external_memory_properties)
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

        *out = format_props.imageFormatProperties;
        if (out_external_memory_properties) {
            *out_external_memory_properties = external_image_props.externalMemoryProperties;
        }

        return true;
    };

    if (check()) {
        *has_mutable_srgb = has_srgb_format;
        return true;
    }

    *has_mutable_srgb = false;

    if (!has_srgb_format) return false;

    // Try without srgb format
    format_list.viewFormatCount = 1;
    format_info.flags = image_flags;
    return check();
}

static
std::vector<VkDrmFormatModifierProperties2EXT> get_drm_modifiers(wren_context* ctx, wren_format format)
{
    VkDrmFormatModifierPropertiesList2EXT mod_list = {
        .sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_2_EXT,
    };
    VkFormatProperties2 props = {
        .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
        .pNext = &mod_list,
    };
    ctx->vk.GetPhysicalDeviceFormatProperties2(ctx->physical_device, format->vk, &props);

    std::vector<VkDrmFormatModifierProperties2EXT> mod_props(mod_list.drmFormatModifierCount);
    mod_list.pDrmFormatModifierProperties = mod_props.data();

    ctx->vk.GetPhysicalDeviceFormatProperties2(ctx->physical_device, format->vk, &props);

    return mod_props;
}

static void load_format_props(wren_context* ctx, wren_format_props& props)
{
    auto format = props.format;

    auto vk_usage = wren_image_usage_to_vk(props.usage);
    auto required_features = wren_get_required_format_features(props.format, props.usage);
    auto has_all_features = [&](VkFormatFeatureFlags features) {
        return (features & required_features) == required_features;
    };

    {
        VkFormatProperties2 vk_props = {
            .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
        };
        ctx->vk.GetPhysicalDeviceFormatProperties2(ctx->physical_device, format->vk, &vk_props);

        VkImageFormatProperties image_props;
        bool has_mutable_srgb = false;
        if (wren_query_image_format_support(ctx, format->vk, format->vk_srgb, vk_usage, nullptr, &has_mutable_srgb, &image_props, nullptr)
                && has_all_features(vk_props.formatProperties.optimalTilingFeatures)) {
            props.opt_props = std::unique_ptr<wren_format_modifier_props>(new wren_format_modifier_props {
                .modifier = DRM_FORMAT_MOD_INVALID,
                .features = vk_props.formatProperties.optimalTilingFeatures,
                .max_extent = {image_props.maxExtent.width, image_props.maxExtent.height},
                .has_mutable_srgb = has_mutable_srgb,
            });
        }
    }

    for (auto& mod : get_drm_modifiers(ctx, format)) {
        VkImageFormatProperties image_props;
        bool has_mutable_srgb = false;
        VkExternalMemoryProperties ext_mem_props;
        if (wren_query_image_format_support(ctx, format->vk, format->vk_srgb, vk_usage, &mod, &has_mutable_srgb, &image_props, &ext_mem_props)
                && has_all_features(mod.drmFormatModifierTilingFeatures)) {
            props.mod_props.emplace_back(wren_format_modifier_props {
                .modifier = mod.drmFormatModifier,
                .features = mod.drmFormatModifierTilingFeatures,
                .plane_count = mod.drmFormatModifierPlaneCount,
                .ext_mem_props = ext_mem_props,
                .max_extent = {image_props.maxExtent.width, image_props.maxExtent.height},
                .has_mutable_srgb = has_mutable_srgb,
            });
            props.mods.emplace_back(mod.drmFormatModifier);
        }
    }
}

const wren_format_props* wren_get_format_props(wren_context* ctx, wren_format format, flags<wren_image_usage> usage)
{
    wrei_assert(!usage.empty());
    auto key = wren_format_props_key{format, usage};
    auto& props = ctx->format_props[key];
    if (!props.format) {
        props.format = format;
        props.usage = usage;
        load_format_props(ctx, props);
    }
    return &props;
}

std::string wren_drm_modifier_get_name(wren_drm_modifier mod)
{
    auto name = drmGetFormatModifierName(mod);
    std::string str = name ?: "UNKNOWN";
    free(name);
    return str;
}

std::vector<wren_drm_modifier> wren_intersect_format_modifiers(std::span<const wren_format_modifier_set* const> sets)
{
    if (sets.empty()) return {};

    std::vector<wren_drm_modifier> out;
    auto first = sets.front();
    auto rest = sets.subspan(1);
    for (auto mod : *first) {
        for (auto set : rest) {
            if (!set->contains(mod)) continue;
        }
        out.emplace_back(mod);
    }
    return out;
}
