#include "server.hpp"

#include "wrei/ref.hpp"

#include "wren/wren.hpp"

#include "wroc/event.hpp"

void wroc_renderer_create(wroc_server* server)
{
    auto* renderer = (server->renderer = wrei_adopt_ref(new wroc_renderer {})).get();
    renderer->server = server;

    renderer->wren = wren_create();

    std::filesystem::path path = getenv("WALLPAPER");

    int w, h;
    int num_channels;
    stbi_uc* data = stbi_load(path.c_str(), &w, &h, &num_channels, STBI_rgb_alpha);
    defer { stbi_image_free(data); };

    log_info("Loaded image ({}, width = {}, height = {})", path.c_str(), w, h);

    renderer->image = wren_image_create(renderer->wren.get(), { u32(w), u32(h) }, VK_FORMAT_R8G8B8A8_UNORM);
    wren_image_update(renderer->image.get(), data);
}

wroc_renderer::~wroc_renderer()
{
    image.reset();
    vkwsi_context_destroy(wren->vkwsi);
    wren.reset();
}

void wroc_render_frame(wroc_output* output)
{
    auto* wren = output->server->renderer->wren.get();
    auto cmd = wren_begin_commands(wren);

    auto current = wroc_output_acquire_image(output);

    wren_transition(wren, cmd, current.image,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        0, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    wren->vk.CmdClearColorImage(cmd, current.image,
        VK_IMAGE_LAYOUT_GENERAL,
        wrei_ptr_to(VkClearColorValue{.float32{0.1f, 0.1f, 0.1f, 1.f}}),
        1, wrei_ptr_to(VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}));

    auto blit = [&](wren_image* image, wrei_vec2i32 offset = {}) {

        wrei_vec2i32 extent = {image->extent.width, image->extent.height};

        wrei_vec2i32 src_pos = {};
        wrei_vec2i32 dst_pos = offset;

        i32 overlap;

        if ((overlap = -dst_pos.x) > 0) {
            src_pos.x += overlap;
            dst_pos.x = 0;
            extent.x -= overlap;
        }

        if ((overlap = -dst_pos.y) > 0) {
            src_pos.y += overlap;
            dst_pos.y = 0;
            extent.y -= overlap;
        }

        if ((overlap = dst_pos.x + extent.x - i32(current.extent.width)) > 0) {
            extent.x -= overlap;
        }

        if ((overlap = dst_pos.y + extent.y - i32(current.extent.height)) > 0) {
            extent.y -= overlap;
        }

        if (extent.x <= 0) return;
        if (extent.y <= 0) return;

        wren->vk.CmdBlitImage2(cmd, wrei_ptr_to(VkBlitImageInfo2 {
            .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
            .srcImage = image->image,
            .srcImageLayout = VK_IMAGE_LAYOUT_GENERAL,
            .dstImage = current.image,
            .dstImageLayout = VK_IMAGE_LAYOUT_GENERAL,
            .regionCount = 1,
            .pRegions = wrei_ptr_to(VkImageBlit2 {
                .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
                .srcSubresource = VkImageSubresourceLayers{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                .srcOffsets = {
                    VkOffset3D {
                        src_pos.x,
                        src_pos.y,
                    },
                    VkOffset3D {
                        src_pos.x + extent.x,
                        src_pos.y + extent.y,
                        1
                    },
                },
                .dstSubresource = VkImageSubresourceLayers{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                .dstOffsets = {
                    VkOffset3D {
                        dst_pos.x,
                        dst_pos.y,
                        0
                    },
                    VkOffset3D {
                        dst_pos.x + extent.x,
                        dst_pos.y + extent.y,
                        1
                    },
                },
            }),
            .filter = VK_FILTER_NEAREST,
        }));
    };

    blit(output->server->renderer->image.get());

    output->server->toplevel_under_cursor.reset();
    for (wroc_surface* surface : output->server->surfaces) {
        if (auto* xdg_surface = wroc_xdg_surface::try_from(surface)) {
            if (surface->current.buffer && surface->current.buffer->image) {
                auto geom = wroc_xdg_surface_get_geometry(xdg_surface);
                blit(surface->current.buffer->image.get(), xdg_surface->position - wrei_vec2f64(geom.origin));
            }
        }
    }

    if (auto* pointer = output->server->seat->pointer) {
        for (wroc_surface* surface : output->server->surfaces) {
            if (!surface->current.buffer) continue;
            if (auto* toplevel = wroc_xdg_toplevel::try_from(surface)) {
                auto geom = wroc_xdg_surface_get_geometry(toplevel->base.get());
                auto surface_position = pointer->layout_position - toplevel->base->position + wrei_vec2f64(geom.origin);
                if (wroc_surface_point_accepts_input(surface, surface_position)) {
                    output->server->toplevel_under_cursor = wrei_weak_from(toplevel);
                }
            }
        }
    }

    wren_transition(wren, cmd, current.image,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, 0,
        VK_ACCESS_2_TRANSFER_WRITE_BIT, 0,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    wren_submit_commands(wren, cmd);
    wren_check(vkwsi_swapchain_present(&output->swapchain, 1, wren->queue, nullptr, 0, false));

    auto elapsed = wroc_get_elapsed_milliseconds(output->server);

    for (wroc_surface* surface : output->server->surfaces) {
        while (auto* callback = surface->current.frame_callbacks.front()) {
            // log_trace("Sending frame callback: {}", (void*)callback);
            wl_callback_send_done(callback, elapsed);
            wl_resource_destroy(callback);
        }
    }
}
