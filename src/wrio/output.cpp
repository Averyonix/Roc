#include "internal.hpp"

wrio_output::~wrio_output()
{
    wrio_output_remove(this);
}

void wrio_output_add(wrio_output* output)
{
    wrei_assert(!std::ranges::contains(output->ctx->outputs, output));
    output->ctx->outputs.emplace_back(output);
    wrio_post_event(wrei_ptr_to(wrio_event {
        .ctx = output->ctx,
        .type = wrio_event_type::output_added,
        .output = {
            .output = output
        },
    }));
}

void wrio_output_remove(wrio_output* output)
{
    if (std::erase(output->ctx->outputs, output)) {
        wrio_post_event(wrei_ptr_to(wrio_event {
            .ctx = output->ctx,
            .type = wrio_event_type::output_removed,
            .output = {
                .output = output
            },
        }));
    }
}

// -----------------------------------------------------------------------------

static
ref<wren_image> acquire(wrio_output* output)
{
    auto& swapchain = output->swapchain;

    // Clear out any incorrectly sized images
    std::erase_if(swapchain.free_images, [&](auto& i) { return i->extent != output->size; });

    // Clear out any other free images if we have too many in flight
    auto total_images = swapchain.free_images.size() + swapchain.images_in_flight;
    if (total_images > swapchain.max_images && !swapchain.free_images.empty()) {
        swapchain.free_images.erase(swapchain.free_images.end()
            - std::min<usz>(swapchain.free_images.size(), total_images - swapchain.max_images));
    }

    // Return nullptr if we can't allocate a new swapchain image
    if (swapchain.free_images.empty() && swapchain.images_in_flight >= swapchain.max_images) {
        return nullptr;
    }

    swapchain.images_in_flight++;

    if (!swapchain.free_images.empty()) {
        auto image = std::move(swapchain.free_images.back());
        swapchain.free_images.pop_back();
        return image;
    }

    log_warn("Creating new swapchain image {}", wrei_to_string(output->size));

    auto wren = output->ctx->wren.get();

    auto format = wren_format_from_drm(DRM_FORMAT_ABGR8888);
    auto usage = wren_image_usage::transfer_dst | wren_image_usage::render;
    auto mods = wren_get_format_props(wren, format, usage)->mods;
    auto image = wren_image_create_dmabuf(wren, output->size, format, usage, mods);

    return image;
}

static
void release(wrio_output* output, u32 slot_idx, u64 signalled)
{
    auto& slot = output->swapchain.release_slots[slot_idx];

    wrei_assert(signalled == slot.release_point);

    output->swapchain.free_images.emplace_back(std::move(slot.image));
    output->swapchain.images_in_flight--;

    wrio_output_try_render(output);
}

static
void present(wrio_output* output, wren_image* image, wren_syncpoint acquire)
{
    auto& swapchain = output->swapchain;

    auto slot = std::ranges::find_if(swapchain.release_slots, [](auto& s) { return !s.image; });
    if (slot == swapchain.release_slots.end()) {
        slot = swapchain.release_slots.insert(swapchain.release_slots.end(), wrio_swapchain::release_slot {
            .semaphore = wren_semaphore_create(output->ctx->wren.get()),
        });
    }

    slot->image = image;
    slot->release_point++;

    flags<wrio_output_commit_flag> flags = {};
    flags |= wrio_output_commit_flag::vsync;
    output->commit(image, acquire, {slot->semaphore.get(), slot->release_point}, flags);

    auto slot_idx = std::distance(swapchain.release_slots.begin(), slot);
    wren_semaphore_wait_value(slot->semaphore.get(), slot->release_point,
        [output = weak(output), slot_idx](u64 signalled) {
            if (output) release(output.get(), slot_idx,signalled);
        });
}

static
auto render(wrio_context* ctx, wren_image* image) -> wren_syncpoint
{
    auto wren = ctx->wren.get();

    auto queue = wren_get_queue(wren, wren_queue_type::graphics);
    auto commands = wren_commands_begin(queue);
    wren->vk.CmdClearColorImage(commands->buffer, image->image, VK_IMAGE_LAYOUT_GENERAL,
        wrei_ptr_to(VkClearColorValue{.float32{1,0,0,1}}),
        1, wrei_ptr_to(VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}));

    return wren_commands_submit(commands.get(), {});
}


void wrio_output_try_render(wrio_output* output)
{
    auto* ctx = output->ctx;

    if (!output->commit_available) return;

    auto image = acquire(output);
    if (!image) return;

    auto done = render(ctx, image.get());

    present(output, image.get(), done);
}
