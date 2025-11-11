#include "server.hpp"

#include "renderer/renderer.hpp"
#include "renderer/vulkan_context.hpp"
#include "renderer/vulkan_helpers.hpp"

#include "protocol/protocol.hpp"

u32 server_get_elapsed_milliseconds(Server* server)
{
    // TODO: This will elapse after 46 days of runtime, should we base it on surface epoch?

    auto now = std::chrono::steady_clock::now();
    auto elapsed = now - server->epoch;
    return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
}

void server_run(int /* argc */, char* /* argv */[])
{
    Server server = {};
    log_warn("server = {}", (void*)&server);

    server.epoch = std::chrono::steady_clock::now();

    setenv("WAYLAND_DEBUG", "1", true);
    server.display = wl_display_create();
    unsetenv("WAYLAND_DEBUG");
    server.event_loop = wl_display_get_event_loop(server.display);

    backend_init(&server);
    renderer_init(&server);

    const char* socket = wl_display_add_socket_auto(server.display);

    wl_global_create(server.display, &wl_compositor_interface, wl_compositor_interface.version, &server, bind_wl_compositor);
    wl_global_create(server.display, &wl_shm_interface,        wl_shm_interface.version,        &server, bind_wl_shm);
    wl_global_create(server.display, &xdg_wm_base_interface,   xdg_wm_base_interface.version,   &server, bind_xdg_wm_base);

    log_info("Running compositor on: {}", socket);

    wl_display_run(server.display);

    log_info("Compositor shutting down");

    if (server.backend) {
        backend_destroy(server.backend);
    }
}

void server_terminate(Server* server)
{
    wl_display_terminate(server->display);
}

void output_added(Output* /* output */)
{
    log_debug("Output added");
}

void output_removed(Output* /* output */)
{
    log_debug("Output removed");
}

void output_frame(Output* output)
{
    auto* vk = output->server->renderer->vk;
    auto cmd = vulkan_context_begin_commands(vk);

    auto current = output_acquire_image(output);

    vk_transition(vk, cmd, current.image,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        0, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    vk->CmdClearColorImage(cmd, current.image,
        VK_IMAGE_LAYOUT_GENERAL,
        ptr_to(VkClearColorValue{.float32{0.1f, 0.1f, 0.1f, 1.f}}),
        1, ptr_to(VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}));

    auto blit = [&](const VulkanImage& image) {
        vk->CmdBlitImage2(cmd, ptr_to(VkBlitImageInfo2 {
            .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
            .srcImage = image.image,
            .srcImageLayout = VK_IMAGE_LAYOUT_GENERAL,
            .dstImage = current.image,
            .dstImageLayout = VK_IMAGE_LAYOUT_GENERAL,
            .regionCount = 1,
            .pRegions = ptr_to(VkImageBlit2 {
                .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
                .srcSubresource = VkImageSubresourceLayers{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                .srcOffsets = {
                    VkOffset3D { },
                    VkOffset3D { i32(image.extent.width), i32(image.extent.height), 1 },
                },
                .dstSubresource = VkImageSubresourceLayers{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                .dstOffsets = {
                    VkOffset3D { },
                    VkOffset3D {
                        i32(std::min(current.extent.width, image.extent.width)),
                        i32(std::min(current.extent.height, image.extent.height)),
                        1
                    },
                },
            }),
            .filter = VK_FILTER_NEAREST,
        }));
    };

    blit(output->server->renderer->image);

    for (Surface* surface : output->server->surfaces) {
        if (surface->current_image.image) {
            blit(surface->current_image);
        }
    }

    vk_transition(vk, cmd, current.image,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, 0,
        VK_ACCESS_2_TRANSFER_WRITE_BIT, 0,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    vulkan_context_submit_commands(vk, cmd);
    vk_check(vkwsi_swapchain_present(&output->swapchain, 1, vk->queue, nullptr, 0, false));

    auto elapsed = server_get_elapsed_milliseconds(output->server);

    for (Surface* surface : output->server->surfaces) {
        if (surface->frame_callback) {
            wl_callback_send_done(surface->frame_callback, elapsed);
            wl_resource_destroy(surface->frame_callback);
        }
    }
}
