#include "server.hpp"

#include "wren/wren.hpp"
#include "wren/wren_helpers.hpp"

u32 wroc_server_get_elapsed_milliseconds(wroc_server* server)
{
    // TODO: This will elapse after 46 days of runtime, should we base it on surface epoch?

    auto now = std::chrono::steady_clock::now();
    auto elapsed = now - server->epoch;
    return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
}

void wroc_run(int /* argc */, char* /* argv */[])
{
    wroc_server server = {};
    log_warn("server = {}", (void*)&server);

    wroc_seat seat = {};
    seat.name = "seat-0";
    server.seat = &seat;

    server.epoch = std::chrono::steady_clock::now();

    setenv("WAYLAND_DEBUG", "1", true);
    server.display = wl_display_create();
    unsetenv("WAYLAND_DEBUG");
    server.event_loop = wl_display_get_event_loop(server.display);

    wroc_backend_init(&server);
    wroc_renderer_create(&server);

    const char* socket = wl_display_add_socket_auto(server.display);

    wl_global_create(server.display, &wl_compositor_interface, wl_compositor_interface.version, &server, wroc_wl_compositor_bind_global);
    wl_global_create(server.display, &wl_shm_interface,        wl_shm_interface.version,        &server, wroc_wl_shm_bind_global);
    wl_global_create(server.display, &xdg_wm_base_interface,   xdg_wm_base_interface.version,   &server, wroc_xdg_wm_base_bind_global);
    wl_global_create(server.display, &wl_seat_interface,       wl_seat_interface.version,       &seat,   wroc_wl_seat_bind_global);

    wl_global_create(server.display, &zwp_linux_dmabuf_v1_interface, 3/* zwp_linux_dmabuf_v1_interface.version */, &server, wroc_zwp_linux_dmabuf_v1_bind_global);

    log_info("Running compositor on: {}", socket);

    wl_display_run(server.display);

    log_info("Compositor shutting down");

    if (server.backend) {
        wroc_backend_destroy(server.backend);
    }

    wl_display_destroy_clients(server.display);

    if (server.renderer) {
        wroc_renderer_destroy(&server);
    }

    wl_display_destroy(server.display);

    log_info("Shutdown complete");
}

void wroc_terminate(wroc_server* server)
{
    wl_display_terminate(server->display);
}

void wroc_output_frame(wroc_output* output)
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

    auto blit = [&](wren_image* image) {
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
                    VkOffset3D { },
                    VkOffset3D { i32(image->extent.width), i32(image->extent.height), 1 },
                },
                .dstSubresource = VkImageSubresourceLayers{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                .dstOffsets = {
                    VkOffset3D { },
                    VkOffset3D {
                        i32(std::min(current.extent.width, image->extent.width)),
                        i32(std::min(current.extent.height, image->extent.height)),
                        1
                    },
                },
            }),
            .filter = VK_FILTER_NEAREST,
        }));
    };

    blit(output->server->renderer->image.get());

    for (wroc_surface* surface : output->server->surfaces) {
        if (surface->current.buffer) {
            blit(surface->current.buffer->image.get());
        }
    }

    wren_transition(wren, cmd, current.image,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, 0,
        VK_ACCESS_2_TRANSFER_WRITE_BIT, 0,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    wren_submit_commands(wren, cmd);
    wren_check(vkwsi_swapchain_present(&output->swapchain, 1, wren->queue, nullptr, 0, false));

    auto elapsed = wroc_server_get_elapsed_milliseconds(output->server);

    for (wroc_surface* surface : output->server->surfaces) {
        if (surface->frame_callback) {
            log_trace("Sending frame callback");
            wl_callback_send_done(surface->frame_callback, elapsed);
            wl_resource_destroy(surface->frame_callback);
        }
    }
}
