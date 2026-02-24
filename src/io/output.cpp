#include "internal.hpp"

auto io_list_outputs(io_context* ctx) -> std::span<io_output* const>
{
    return ctx->outputs;
}

io_output::~io_output()
{
    io_output_remove(this);
}

void io_output_add(io_output* output)
{
    core_assert(!std::ranges::contains(output->ctx->outputs, output));
    output->ctx->outputs.emplace_back(output);
    io_post_event(ptr_to(io_event {
        .ctx = output->ctx,
        .type = io_event_type::output_added,
        .output = {
            .output = output
        },
    }));
}

void io_output_remove(io_output* output)
{
    if (std::erase(output->ctx->outputs, output)) {
        io_post_event(ptr_to(io_event {
            .ctx = output->ctx,
            .type = io_event_type::output_removed,
            .output = {
                .output = output
            },
        }));
    }
}

// -----------------------------------------------------------------------------

static
ref<gpu_image> acquire(io_output* output)
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

    log_warn("Creating new swapchain image {}", core_to_string(output->size));

    auto gpu = output->ctx->gpu;

    auto format = gpu_format_from_drm(DRM_FORMAT_ABGR8888);
    auto mods = gpu_get_format_props(gpu, format, output->requested_usage)->mods;
    auto image = gpu_image_create_dmabuf(gpu, output->size, format, output->requested_usage, mods);

    return image;
}

static
void release(io_output* output, u32 slot_idx, u64 signalled)
{
    auto& slot = output->swapchain.release_slots[slot_idx];

    core_assert(signalled == slot.release_point);

    output->swapchain.free_images.emplace_back(std::move(slot.image));
    output->swapchain.images_in_flight--;

    io_output_try_redraw(output);
}

void io_output_present(io_output* output, gpu_image* image, gpu_syncpoint acquire)
{
    auto& swapchain = output->swapchain;

    auto slot = std::ranges::find_if(swapchain.release_slots, [](auto& s) { return !s.image; });
    if (slot == swapchain.release_slots.end()) {
        slot = swapchain.release_slots.insert(swapchain.release_slots.end(), io_swapchain::release_slot {
            .semaphore = gpu_semaphore_create(output->ctx->gpu),
        });
    }

    slot->image = image;
    slot->release_point++;

    flags<io_output_commit_flag> flags = {};
    flags |= io_output_commit_flag::vsync;
    output->commit(image, acquire, {slot->semaphore.get(), slot->release_point}, flags);

    auto slot_idx = std::distance(swapchain.release_slots.begin(), slot);
    gpu_semaphore_wait_value(slot->semaphore.get(), slot->release_point,
        [output = weak(output), slot_idx](u64 signalled) {
            if (output) release(output.get(), slot_idx,signalled);
        });
}

void io_output_try_redraw(io_output* output)
{
    if (!output->frame_requested) return;
    if (!output->commit_available) return;
    if (!output->size.x || !output->size.y) return;

    auto image = acquire(output);
    if (!image) return;

    output->frame_requested = false;

    io_post_event(ptr_to(io_event {
        .ctx = output->ctx,
        .type = io_event_type::output_redraw,
        .output = {
            .output = output,
            .target = image.get(),
        }
    }));
}

void io_output_try_redraw_later(io_output* output)
{
    core_event_loop_enqueue(output->ctx->event_loop, [output = weak(output)] {
        if (output) {
            io_output_try_redraw(output.get());
        }
    });
}

void io_output_request_frame(io_output* output, flags<gpu_image_usage> usage)
{
    output->frame_requested = true;
    output->requested_usage = usage;
    io_output_try_redraw_later(output);
}

auto io_output_get_size(io_output* output) -> vec2u32
{
    return output->size;
}

void io_output_post_configure(io_output* output)
{
    io_post_event(ptr_to(io_event {
        .ctx = output->ctx,
        .type = io_event_type::output_configure,
        .output = {
            .output = output,
        }
    }));
}
