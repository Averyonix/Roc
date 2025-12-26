#include "server.hpp"
#include "util.hpp"

const u32 wroc_wp_viewporter_version = 1;

// -----------------------------------------------------------------------------

static
void wroc_wp_viewporter_get_viewport(wl_client* client, wl_resource* resource, u32 id, wl_resource* _surface)
{
    auto surface = wroc_get_userdata<wroc_surface>(_surface);
    log_warn("wp_viewporter::get_viewporter(surface = {})", (void*)surface);
    auto new_resource = wroc_resource_create(client, &wp_viewport_interface, wl_resource_get_version(resource), id);
    auto viewport = wrei_create_unsafe<wroc_viewport>();
    viewport->resource = resource;
    wroc_surface_put_addon(surface, viewport);
    wroc_resource_set_implementation_refcounted(new_resource, &wroc_wp_viewport_impl, viewport);
}

const struct wp_viewporter_interface wroc_wp_viewporter_impl
{
    .destroy = wroc_simple_resource_destroy_callback,
    .get_viewport = wroc_wp_viewporter_get_viewport,
};

void wroc_wp_viewporter_bind_global(wl_client* client, void* data, u32 version, u32 id)
{
    log_warn("wp_viewporter::bind_global()");
    auto* server = static_cast<wroc_server*>(data);
    auto new_resource = wroc_resource_create(client, &wp_viewporter_interface, version, id);
    wroc_resource_set_implementation(new_resource, &wroc_wp_viewporter_impl, server);
}

// -----------------------------------------------------------------------------

static
void wroc_wp_viewport_set_source(wl_client* client, wl_resource* resource, wl_fixed_t x, wl_fixed_t y, wl_fixed_t width, wl_fixed_t height)
{
    auto* viewport = wroc_get_userdata<wroc_viewport>(resource);
    viewport->pending.committed |= wroc_viewport_committed_state::source;
    viewport->pending.source = {{wl_fixed_to_double(x), wl_fixed_to_double(y)}, {wl_fixed_to_double(width), wl_fixed_to_double(height)}};
}

static
void wroc_wp_viewport_set_destination(wl_client* client, wl_resource* resource, i32 width, i32 height)
{
    auto* viewport = wroc_get_userdata<wroc_viewport>(resource);
    viewport->pending.committed |= wroc_viewport_committed_state::destination;
    viewport->pending.destination = {width, height};
}

const struct wp_viewport_interface wroc_wp_viewport_impl
{
    .destroy = wroc_surface_addon_destroy,
    .set_source = wroc_wp_viewport_set_source,
    .set_destination = wroc_wp_viewport_set_destination,
};

void wroc_viewport::on_commit(wroc_surface_commit_flags)
{
    if (pending.committed >= wroc_viewport_committed_state::source) {
        if (pending.source == rect2f64{{-1, -1}, {-1, -1}}) {
            log_debug("wp_viewport source unset");
            current.committed -= wroc_viewport_committed_state::source;
        } else {
            log_debug("wp_viewport source = {}", wrei_to_string(pending.source));
            current.source = pending.source;
            current.committed |= wroc_viewport_committed_state::source;
        }
    }

    if (pending.committed >= wroc_viewport_committed_state::destination) {
        if (pending.destination == vec2i32{-1, -1}) {
            log_debug("wp_viewport destination unset");
            current.committed -= wroc_viewport_committed_state::destination;
        } else {
            log_debug("wp_viewport destination = {}", wrei_to_string(pending.destination));
            current.destination = pending.destination;
            current.committed |= wroc_viewport_committed_state::destination;
        }
    }

    pending.committed = {};

    if (current.committed >= wroc_viewport_committed_state::source) {
        surface->buffer_src = current.source;
    }
    if (current.committed >= wroc_viewport_committed_state::destination) {
        surface->buffer_dst.extent = current.destination;
    } else if (current.committed >= wroc_viewport_committed_state::source) {
        surface->buffer_dst.extent = current.source.extent;
    }

    // log_debug("buffer src = {}, dst = {}", wrei_to_string(surface->buffer_src), wrei_to_string(surface->buffer_dst));
}
