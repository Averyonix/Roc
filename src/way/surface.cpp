#include "internal.hpp"

static
void create_surface(wl_client* client, wl_resource* resource, u32 id)
{
    auto surface = core_create<way_surface>();
    surface->server = way_get_userdata<way_server>(resource);

    surface->pending = &surface->cached.emplace_back();

    surface->wl_surface = way_resource_create_refcounted(wl_surface, client, resource, id, surface.get());
}

WAY_INTERFACE(wl_compositor) = {
    .create_surface = create_surface,
    WAY_STUB(create_region),
};

WAY_BIND_GLOBAL(wl_compositor)
{
    way_resource_create(wl_compositor, client, version, id, way_get_userdata<way_server>(data));
}

// -----------------------------------------------------------------------------

static
void attach(wl_client* client, wl_resource* resource, wl_resource* wl_buffer, i32 x, i32 y)
{
    auto* surface = way_get_userdata<way_surface>(resource);
    auto* pending = surface->pending;

    pending->buffer.lock = nullptr;
    pending->buffer.handle = wl_buffer ? way_get_userdata<way_buffer>(wl_buffer) : nullptr;
    pending->committed.insert(way_surface_committed_state::buffer);

    if (x || y) {
        if (wl_resource_get_version(resource) >= WL_SURFACE_OFFSET_SINCE_VERSION) {
            way_post_error(surface->server, resource, WL_SURFACE_ERROR_INVALID_OFFSET,
                "Non-zero offset not allowed in wl_surface::attach since version {}", WL_SURFACE_OFFSET_SINCE_VERSION);
        } else {
            pending->surface.delta = { x, y };
            pending->committed.insert(way_surface_committed_state::offset);
        }
    }
}

way_surface_state::~way_surface_state()
{
    // TODO: Empty callbacks
}

static
void surface_set_mapped(way_surface* surface, bool mapped)
{
    if (mapped == surface->mapped) return;
    surface->mapped = mapped;

    log_info("Surface {} was {}", (void*)surface, mapped ? "mapped" : "unmapped");

    if (mapped) {
        auto* server = surface->server;
        auto* scene = server->scene;
        auto* buffer = surface->current.buffer.handle.get();

        surface->window = scene_window_create(server->client.get());
        scene_window_set_frame(surface->window.get(), {{}, buffer->extent, core_xywh});

        auto texture = scene_texture_create(scene);
        surface->texture = texture;
        scene_texture_set_image(texture.get(), buffer->image.get(), server->sampler.get(), gpu_blend_mode::premultiplied);
        scene_texture_set_dst(texture.get(), {{}, buffer->extent, core_xywh});

        scene_node_set_transform(texture.get(), scene_window_get_transform(surface->window.get()));
        scene_tree_place_above(scene_window_get_tree(surface->window.get()), nullptr, texture.get());

        scene_window_map(surface->window.get());
    } else {
        surface->window = nullptr;
        surface->texture = nullptr;
    }

    // if (!mapped) {
    //     if (surface == server->seat->keyboard->focused_surface.get()) {
    //         refocus_on_unmap();
    //     }

    //     if (surface == server->seat->pointer->focused_surface.get()) {
    //         // TODO: Re-check surface under pointer
    //         server->seat->pointer->focused_surface = nullptr;
    //     }
    // }
}


static
void update_map_state(way_surface* surface)
{
    bool can_be_mapped =
           surface->current.buffer.handle
        && surface->current.buffer.handle->image
        && surface->wl_surface;

    surface_set_mapped(surface, can_be_mapped);
}

static
void apply(way_surface* surface, way_surface_state& from)
{
    surface->current.commit = from.commit;

    WAY_ADDON_SIMPLE_STATE_APPLY(from, surface->current, buffer.transform, buffer_transform);
    WAY_ADDON_SIMPLE_STATE_APPLY(from, surface->current, buffer.scale,     buffer_scale);

    if (from.committed.contains(way_surface_committed_state::buffer)) {
        if (from.buffer.handle && from.buffer.handle->resource) {
            surface->current.buffer.handle = std::move(from.buffer.handle);
            surface->current.buffer.lock   = std::move(from.buffer.lock);
        } else {
            if (from.buffer.handle) {
                log_warn("Pending buffer was destroyed, surface contents will be cleared");
            }
            surface->current.buffer.handle = nullptr;
            surface->current.buffer.lock = nullptr;
        }

        from.buffer.handle = nullptr;
        from.buffer.lock = nullptr;
    }

    way_xdg_surface_apply(surface, from);
    way_toplevel_apply(   surface, from);
    // way_subsurface_apply( surface, from);

    surface->current.committed.insert_range(from.committed);
}

static
void flush(way_surface* surface)
{
    // TODO: Queued applications

    auto prev_applied_commit_id = surface->current.commit;

    while (surface->cached.size() > 1) {
        auto& packet = surface->cached.front();

        // TODO: Subsurface parent commit dependencies

        // Check for buffer ready
        if (packet.buffer.lock && !packet.buffer.lock->buffer->is_ready(surface)) {
            core_debugkill();
        }

        apply(surface, packet);

        surface->cached.pop_front();
    }

    if (surface->current.commit == prev_applied_commit_id) return;

    // Flush subsurface state recursively

    if (surface->stack.size() > 1) {
        for (auto* s : surface->stack) {
            if (s != surface) {
                flush(s);
            }
        }
    }

    if (surface->current.buffer.handle && surface->texture) {
        scene_texture_set_image(surface->texture.get(), surface->current.buffer.handle->image.get(), surface->server->sampler.get(), gpu_blend_mode::premultiplied);
    }

    update_map_state(surface);
}

static
void commit(wl_client* client, wl_resource* resource)
{
    auto* surface = way_get_userdata<way_surface>(resource);

    auto* pending = surface->pending;
    pending->commit = ++surface->last_commit_id;
    surface->pending = &surface->cached.emplace_back();

    // Begin acquisition process for buffers

    if (pending->committed.contains(way_surface_committed_state::buffer)) {
        if (pending->buffer.handle) {
            pending->buffer.lock = pending->buffer.handle->commit(surface);
        }
    }

    // Attempt to flush any state immediately

    flush(surface);
}

WAY_INTERFACE(wl_surface) = {
    .destroy = way_simple_destroy,
    .attach = attach,
    WAY_STUB(damage),
    WAY_STUB(frame),
    WAY_STUB(set_opaque_region),
    WAY_STUB(set_input_region),
    .commit = commit,
    .set_buffer_transform = WAY_ADDON_SIMPLE_STATE_REQUEST(way_surface, buffer.transform, buffer_transform, wl_output_transform(bt), i32 bt),
    .set_buffer_scale     = WAY_ADDON_SIMPLE_STATE_REQUEST(way_surface, buffer.scale,     buffer_scale,     scale,                   i32 scale),
    WAY_STUB(damage_buffer),
    WAY_STUB(offset),
};

// -----------------------------------------------------------------------------

way_surface::~way_surface()
{
}
