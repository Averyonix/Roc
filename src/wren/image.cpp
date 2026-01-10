#include "internal.hpp"

#include "wrei/util.hpp"

void wren_transition(wren_context* ctx, wren_commands* commands, wren_image* image,
    VkPipelineStageFlags2 src, VkPipelineStageFlags2 dst,
    VkAccessFlags2 src_access, VkAccessFlags2 dst_access,
    VkImageLayout old_layout, VkImageLayout new_layout)
{
    ctx->vk.CmdPipelineBarrier2(commands->buffer, wrei_ptr_to(VkDependencyInfo {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = wrei_ptr_to(VkImageMemoryBarrier2 {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = src,
            .srcAccessMask = src_access,
            .dstStageMask = dst,
            .dstAccessMask = dst_access,
            .oldLayout = old_layout,
            .newLayout = new_layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image->image,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        }),
    }));
}

VkImageUsageFlags wren_image_usage_to_vk(wren_image_usage usage)
{
    VkImageUsageFlags flags = {};
    if (usage >= wren_image_usage::render)   flags |= wren_render_usage;
    if (usage >= wren_image_usage::texture)  flags |= wren_dma_texture_usage;
    if (usage >= wren_image_usage::transfer) flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    return flags;
}

// -----------------------------------------------------------------------------

wren_image::~wren_image()
{
    ctx->image_descriptor_allocator.free(id);
}

// -----------------------------------------------------------------------------

struct wren_image_vma : wren_image
{
    VmaAllocation vma_allocation;

    struct {
        usz allocation_size;
    } stats;

    ~wren_image_vma();
};

wren_image_vma::~wren_image_vma()
{
    ctx->stats.active_images--;
    ctx->stats.active_image_memory -= stats.allocation_size;

    ctx->vk.DestroyImageView(ctx->device, view, nullptr);
    vmaDestroyImage(ctx->vma, image, vma_allocation);
}

static
ref<wren_image> create_vma_image(wren_context* ctx, vec2u32 extent, wren_format format, wren_image_usage usage)
{
    auto image = wrei_create<wren_image_vma>();
    image->ctx = ctx;

    ctx->stats.active_images++;

    image->extent = extent;
    image->format = format;

    VmaAllocationInfo alloc_info;
    wren_check(vmaCreateImage(ctx->vma, wrei_ptr_to(VkImageCreateInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format->vk,
        .extent = {extent.x, extent.y, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = wren_image_usage_to_vk(usage),
        .sharingMode = VK_SHARING_MODE_CONCURRENT,
        .queueFamilyIndexCount = 2,
        .pQueueFamilyIndices = std::array {
            ctx->graphics_queue->family,
            ctx->transfer_queue->family,
        }.data(),
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    }), wrei_ptr_to(VmaAllocationCreateInfo {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    }), &image->image, &image->vma_allocation, &alloc_info));

    image->stats.allocation_size += alloc_info.size;
    ctx->stats.active_image_memory += alloc_info.size;

    wren_image_init(image.get());

    return image;
}

void wren_image_init(wren_image* image)
{
    auto* ctx = image->ctx;

    wren_check(ctx->vk.CreateImageView(ctx->device, wrei_ptr_to(VkImageViewCreateInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = image->format->vk,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    }), nullptr, &image->view));

    wren_allocate_image_descriptor(image);

    auto queue = wren_get_queue(ctx, wren_queue_type::transfer);
    auto cmd = wren_commands_begin(queue);
    wren_commands_protect_object(cmd.get(), image);
    wren_transition(ctx, cmd.get(), image,
        0, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        0, VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    wren_commands_submit(cmd.get(), {}, {});
}

#if 0
void wren_image_readback(wren_image* image, void* data)
{
    auto* ctx = image->ctx.get();
    auto extent = image->extent;

    auto cmd = wren_commands_begin(ctx);

    constexpr auto pixel_size = 4;
    auto row_length = extent.x;
    auto image_height = row_length * extent.y;
    auto image_size = image_height * pixel_size;

    // TODO: This should be stored persistently for transfers
    ref buffer = wren_buffer_create(ctx, image_size);

    ctx->vk.CmdCopyImageToBuffer(cmd->buffer, image->image, VK_IMAGE_LAYOUT_GENERAL, buffer->buffer, 1, wrei_ptr_to(VkBufferImageCopy {
        .bufferOffset = 0,
        .bufferRowLength = row_length,
        .bufferImageHeight = image_size,
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageOffset = {},
        .imageExtent = { extent.x, extent.y, 1 },
    }));

    wren_commands_submit(cmd.get(), {}, {});

    std::memcpy(data, buffer->host_address, image_size);
}
#endif

void wren_image_update(wren_commands* cmd, wren_image* image, const void* data)
{
    auto* ctx = image->ctx;
    auto extent = image->extent;

    const auto& info = vkuGetFormatInfo(image->format->vk);
    usz block_w = (image->extent.x + info.block_extent.width  - 1) / info.block_extent.width;
    usz block_h = (image->extent.y + info.block_extent.height - 1) / info.block_extent.height;
    usz image_size = block_w * block_h * info.texel_block_size;

    // TODO: This should be stored persistently for transfers
    ref buffer = wren_buffer_create(ctx, image_size);

    wren_commands_protect_object(cmd, image);
    wren_commands_protect_object(cmd, buffer.get());

    std::memcpy(buffer->host_address, data, image_size);

    ctx->vk.CmdCopyBufferToImage(cmd->buffer, buffer->buffer, image->image, VK_IMAGE_LAYOUT_GENERAL, 1, wrei_ptr_to(VkBufferImageCopy {
        .bufferOffset = 0,
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageOffset = {},
        .imageExtent = { extent.x, extent.y, 1 },
    }));
}

void wren_image_update_immed(wren_image* image, const void* data)
{
    auto queue = wren_get_queue(image->ctx, wren_queue_type::transfer);
    auto commands = wren_commands_begin(queue);
    wren_image_update(commands.get(), image, data);
    wren_commands_submit(commands.get(), {}, {});
    wren_wait_idle(queue);
}

// -----------------------------------------------------------------------------

ref<wren_sampler> wren_sampler_create(wren_context* ctx, VkFilter mag, VkFilter min)
{
    ref sampler = wrei_create<wren_sampler>();
    sampler->ctx = ctx;

    ctx->stats.active_samplers++;

    wren_check(ctx->vk.CreateSampler(ctx->device, wrei_ptr_to(VkSamplerCreateInfo {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = mag,
        .minFilter = min,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .anisotropyEnable = false,
        .maxLod = VK_LOD_CLAMP_NONE,
        .borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
    }), nullptr, &sampler->sampler));

    wren_allocate_sampler_descriptor(sampler.get());

    return sampler;
}

wren_sampler::~wren_sampler()
{
    ctx->stats.active_samplers--;

    ctx->sampler_descriptor_allocator.free(id);

    ctx->vk.DestroySampler(ctx->device, sampler, nullptr);
}

// -----------------------------------------------------------------------------

u32 wren_find_vk_memory_type_index(wren_context* ctx, u32 type_filter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties props;
    ctx->vk.GetPhysicalDeviceMemoryProperties(ctx->physical_device, &props);

    for (u32 i = 0; i < props.memoryTypeCount; ++i) {
        std::string flags;
        if (!(type_filter & (1 << i))) continue;
        if ((props.memoryTypes[i].propertyFlags & properties) != properties) continue;

        return i;
    }

    log_error("Failed to find suitable memory type");
    return ~0u;
}

// -----------------------------------------------------------------------------

wren_image_dmabuf::~wren_image_dmabuf()
{
    ctx->stats.active_images--;
    ctx->stats.active_image_memory -= stats.allocation_size;

    ctx->vk.DestroyImageView(ctx->device, view, nullptr);
    ctx->vk.DestroyImage(ctx->device, image, nullptr);
    for (auto& plane : dma_params.planes) {
        close(plane.fd);
    }
    for (auto memory : memory_planes) {
        ctx->vk.FreeMemory(ctx->device, memory, nullptr);
    }
}

static
bool is_dmabuf_disjoint(const wren_dma_params& params)
{
    auto& planes = params.planes;
    if (planes.count == 1) return false;

    struct stat first = {};
    if (wrei_unix_check_n1(fstat(planes[0].fd, &first)) != 0) return true;

    for (u32 i = 1; i < planes.count; ++i) {
        struct stat other = {};
        if (wrei_unix_check_n1(fstat(planes[i].fd, &other)) != 0) return true;

        if (first.st_ino != other.st_ino) return true;
    }

    return false;
}

static
const wren_format_modifier_props* get_modifier_props(wren_context* ctx, wren_format format, wren_drm_modifier modifier, bool render)
{
    auto* props = wren_get_format_props(ctx, format);
    for (auto& mod_props : render ? props->dmabuf.render_mods : props->dmabuf.render_mods) {
        if (mod_props.props.drmFormatModifier == modifier) {
            return &mod_props;
        }
    }
    return nullptr;
}

static
VkImageAspectFlagBits plane_to_aspect(u32 i)
{
    return std::array {
        VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT,
        VK_IMAGE_ASPECT_MEMORY_PLANE_1_BIT_EXT,
        VK_IMAGE_ASPECT_MEMORY_PLANE_2_BIT_EXT,
        VK_IMAGE_ASPECT_MEMORY_PLANE_3_BIT_EXT
    }[i];
}

ref<wren_image_dmabuf> wren_image_import_dmabuf(wren_context* ctx, const wren_dma_params& params, wren_image_usage usage)
{
    assert(usage != wren_image_usage::none);

    auto props = get_modifier_props(ctx, params.format, params.modifier, usage >= wren_image_usage::render);
    if (!props) {
        log_error("Format {} cannot be used with modifier: {}", params.format->name, wren_drm_modifier_get_name(params.modifier));
        return nullptr;
    }

    auto disjoint = is_dmabuf_disjoint(params);
    if (disjoint && !(props->props.drmFormatModifierTilingFeatures & VK_FORMAT_FEATURE_2_DISJOINT_BIT)) {
        log_error("Format {} with modifier {} does not support disjoint images", params.format->name, wren_drm_modifier_get_name(params.modifier));
        return nullptr;
    }

    auto image = wrei_create<wren_image_dmabuf>();
    image->ctx = ctx;

    ctx->stats.active_images++;

    image->extent = params.extent;
    image->format = params.format;
    image->dma_params = params;
    for (auto& plane : image->dma_params.planes) {
        plane.fd = -1;
    }

    static constexpr auto handle_type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    VkSubresourceLayout plane_layouts[wren_dma_max_planes] = {};
    for (u32 i = 0; i < params.planes.count; ++i) {
        plane_layouts[i].offset = params.planes[i].offset;
        plane_layouts[i].rowPitch = params.planes[i].stride;
    }

    VkImageCreateFlags img_create_flags = {};
    if (disjoint) img_create_flags |= VK_IMAGE_CREATE_DISJOINT_BIT;
    wren_check(ctx->vk.CreateImage(ctx->device, wrei_ptr_to(VkImageCreateInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = wren_vk_make_chain_in({
            wrei_ptr_to(VkExternalMemoryImageCreateInfo {
                .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
                .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
            }),
            wrei_ptr_to(VkImageDrmFormatModifierExplicitCreateInfoEXT {
                .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT,
                .drmFormatModifier = params.modifier,
                .drmFormatModifierPlaneCount = u32(params.planes.count),
                .pPlaneLayouts = plane_layouts,
            }),
        }),
        .flags = img_create_flags,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = params.format->vk,
        .extent = {image->extent.x, image->extent.y, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
        .usage = wren_image_usage_to_vk(usage),
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    }), nullptr, &image->image));

    auto mem_count = disjoint ? params.planes.count : 1;

    VkBindImageMemoryInfo bind_info[wren_dma_max_planes] = {};
    VkBindImagePlaneMemoryInfo plane_info[wren_dma_max_planes] = {};
    log_trace("  planes = {}{}", params.planes.count, disjoint ? " (disjoint)" : "");
    log_trace("  modifier = {}", wren_drm_modifier_get_name(params.modifier));

    for (u32 i = 0; i < mem_count; ++i) {

        auto fd = params.planes[i].fd;
        VkMemoryFdPropertiesKHR fd_props = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR,
        };
        wren_check(ctx->vk.GetMemoryFdPropertiesKHR(ctx->device, handle_type, fd, &fd_props));

        VkMemoryRequirements2 mem_reqs = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
        };
        ctx->vk.GetImageMemoryRequirements2(ctx->device, wrei_ptr_to(VkImageMemoryRequirementsInfo2 {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
            .pNext = disjoint
                ? wrei_ptr_to(VkImagePlaneMemoryRequirementsInfo {
                    .sType = VK_STRUCTURE_TYPE_IMAGE_PLANE_MEMORY_REQUIREMENTS_INFO,
                    .planeAspect = plane_to_aspect(i),
                })
                : nullptr,
            .image = image->image,
        }), &mem_reqs);

        auto mem = wren_find_vk_memory_type_index(ctx, mem_reqs.memoryRequirements.memoryTypeBits & fd_props.memoryTypeBits, 0);

        // Take a first copy, as we don't take ownership of the dmabuf
        image->dma_params.planes[i].fd = wrei_unix_check_n1(fcntl(fd, F_DUPFD_CLOEXEC, 0));

        // Take another copy of the file descriptor, this will be owned by the bound vulkan memory
        int vk_fd = wrei_unix_check_n1(fcntl(fd, F_DUPFD_CLOEXEC, 0));

        log_trace("  mem[{}].fd   = {}", i, vk_fd);
        log_trace("  mem[{}].size = {}", i, wrei_byte_size_to_string(mem_reqs.memoryRequirements.size));
        image->stats.allocation_size   += mem_reqs.memoryRequirements.size;
        ctx->stats.active_image_memory += mem_reqs.memoryRequirements.size;

        if (wren_check(ctx->vk.AllocateMemory(ctx->device, wrei_ptr_to(VkMemoryAllocateInfo {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = wren_vk_make_chain_in({
                wrei_ptr_to(VkImportMemoryFdInfoKHR {
                    .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
                    .handleType = handle_type,
                    .fd = vk_fd,
                }),
                wrei_ptr_to(VkMemoryDedicatedAllocateInfo {
                    .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
                    .image = image->image,
                }),
            }),
            .allocationSize = mem_reqs.memoryRequirements.size,
            .memoryTypeIndex = mem,
        }), nullptr, &image->memory_planes[i])) != VK_SUCCESS) {
            log_error("Failed to import memory");
            close(vk_fd);
            return nullptr;
        }

        bind_info[i].sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
        bind_info[i].image = image->image;
        bind_info[i].memory = image->memory_planes[i];

        if (disjoint) {
            plane_info[i].sType = VK_STRUCTURE_TYPE_BIND_IMAGE_PLANE_MEMORY_INFO;
            plane_info[i].planeAspect = plane_to_aspect(i);
            bind_info[i].pNext = &plane_info[i];
        }
    }

    wren_check(ctx->vk.BindImageMemory2(ctx->device, params.planes.count, bind_info));

    wren_image_init(image.get());

    return image;
}

// -----------------------------------------------------------------------------

void wren_init_gbm_allocator(wren_context* ctx)
{
    log_debug("DRM fd: {}", ctx->drm_fd);
    ctx->gbm = wrei_unix_check_null(gbm_create_device(ctx->drm_fd));

    log_debug("Created GBM allocator {} with backend {}", (void*)ctx->gbm, gbm_device_get_backend_name(ctx->gbm));
}

void wren_destroy_gbm_allocator(wren_context* ctx)
{
    if (ctx->gbm) {
        gbm_device_destroy(ctx->gbm);
    }
}

[[maybe_unused]] static
ref<wren_image> create_gbm_image(wren_context* ctx, vec2u32 extent, wren_format format, wren_image_usage usage)
{
    // Insert format modifiers

    std::vector<const wren_format_modifier_set*> sets;
    if (usage >= wren_image_usage::render)  sets.emplace_back(&ctx->dmabuf_render_formats.entries.at(format));
    if (usage >= wren_image_usage::texture) sets.emplace_back(&ctx->dmabuf_texture_formats.entries.at(format));
    auto mods = wren_intersect_format_modifiers(sets);

    // Allocate GBM buffer

    u32 flags = 0;
    if (usage >= wren_image_usage::render)  flags |= GBM_BO_USE_RENDERING;
    if (usage >= wren_image_usage::scanout) flags |= GBM_BO_USE_SCANOUT;
    if (usage >= wren_image_usage::cursor)  flags |= GBM_BO_USE_SCANOUT;

    gbm_bo* bo = gbm_bo_create_with_modifiers2(ctx->gbm, extent.x, extent.y, format->drm, mods.data(), mods.size(), flags);
    if (!bo) {
        log_error("Failed to allocate GBM buffer");
        return nullptr;
    }
    defer {
        gbm_bo_destroy(bo);
    };

    // Export DMA-BUF parameters

    wren_dma_params params = {};
    params.extent = extent;
    params.format = format;
    params.modifier     = gbm_bo_get_modifier(bo);
    params.planes.count = gbm_bo_get_plane_count(bo);
    for (u32 i = 0; i < params.planes.count; ++i) {
        params.planes[i].offset = gbm_bo_get_offset(bo, i);
        params.planes[i].stride = gbm_bo_get_stride_for_plane(bo, i);
        params.planes[i].fd     = gbm_bo_get_fd_for_plane(bo, i);
    }
    defer {
        // `wren_image_import_dmabuf` duplicates the fds again,
        // so we always close the ones created by `gbm_bo_get_fd_for_plane`
        for (u32 i = 0; i < params.planes.count; ++i) {
            close(params.planes[i].fd);
        }
    };

    log_debug("Allocated GBM image, size = {}, format = {}, mod = {}", wrei_to_string(extent), format->name, wren_drm_modifier_get_name(params.modifier));

    // Import in Vulkan as DMA-BUF image

    return wren_image_import_dmabuf(ctx, params, usage);
}

// -----------------------------------------------------------------------------

#define WROC_PREFER_GBM_IMAGES 0

ref<wren_image> wren_image_create(
    wren_context* ctx,
    vec2u32 extent,
    wren_format format,
    wren_image_usage usage)
{
    assert(usage != wren_image_usage::none);

#if WROC_PREFER_GBM_IMAGES
    return (format->drm != DRM_FORMAT_INVALID && ctx->features >= wren_features::dmabuf)
        ? create_gbm_image(ctx, extent, format, usage)
        : create_vma_image(ctx, extent, format, usage);
#else
    return create_vma_image(ctx, extent, format, usage);
#endif
}
