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

    // Clear out any images that don't meet requirements
    std::erase_if(swapchain.free_images, [&](auto& i) {
        return i->extent != output->size || i->usage != output->requested_usage;
    });

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

    auto wren = output->ctx->wren;

    auto format = wren_format_from_drm(DRM_FORMAT_ABGR8888);
    auto mods = wren_get_format_props(wren, format, output->requested_usage)->mods;
    auto image = wren_image_create_dmabuf(wren, output->size, format, output->requested_usage, mods);

    return image;
}

static
void release(wrio_output* output, u32 slot_idx, u64 signalled)
{
    auto& slot = output->swapchain.release_slots[slot_idx];

    wrei_assert(signalled == slot.release_point);

    output->swapchain.free_images.emplace_back(std::move(slot.image));
    output->swapchain.images_in_flight--;

    wrio_output_try_redraw(output);
}

void wrio_output_present(wrio_output* output, wren_image* image, wren_syncpoint acquire)
{
    auto& swapchain = output->swapchain;

    auto slot = std::ranges::find_if(swapchain.release_slots, [](auto& s) { return !s.image; });
    if (slot == swapchain.release_slots.end()) {
        slot = swapchain.release_slots.insert(swapchain.release_slots.end(), wrio_swapchain::release_slot {
            .semaphore = wren_semaphore_create(output->ctx->wren),
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

void wrio_output_try_redraw(wrio_output* output)
{
    if (!output->frame_requested) return;
    if (!output->commit_available) return;
    if (!output->size.x || !output->size.y) return;

    auto image = acquire(output);
    if (!image) return;

    output->frame_requested = false;

    wrio_post_event(wrei_ptr_to(wrio_event {
        .ctx = output->ctx,
        .type = wrio_event_type::output_redraw,
        .output = {
            .output = output,
            .target = image.get(),
        }
    }));
}

void wrio_output_try_redraw_later(wrio_output* output)
{
    wrei_event_loop_enqueue(output->ctx->event_loop, [output = weak(output)] {
        if (output) {
            wrio_output_try_redraw(output.get());
        }
    });
}

void wrio_output_request_frame(wrio_output* output, flags<wren_image_usage> usage)
{
    output->frame_requested = true;
    output->requested_usage = usage;
    wrio_output_try_redraw_later(output);
}

auto wrio_output_get_size(wrio_output* output) -> vec2u32
{
    return output->size;
}

void wrio_output_post_configure(wrio_output* output)
{
    wrio_post_event(wrei_ptr_to(wrio_event {
        .ctx = output->ctx,
        .type = wrio_event_type::output_configure,
        .output = {
            .output = output,
        }
    }));
}
