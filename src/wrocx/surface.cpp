#include "internal.hpp"

WROC_NAMESPACE_BEGIN

static
void create_surface(wl_client* client, wl_resource* resource, u32 id)
{
    auto surface = wrei_create<wroc_surface>();
    surface->server = wroc_get_userdata<wroc_server>(resource);

    surface->pending = &surface->cached.emplace_back();

    surface->wl_surface = wroc_resource_create_refcounted(wl_surface, client, resource, id, surface.get());
}

WROC_INTERFACE(wl_compositor) = {
    .create_surface = create_surface,
    WROC_STUB(create_region),
};

WROC_BIND_GLOBAL(wl_compositor)
{
    wroc_resource_create(wl_compositor, client, version, id, wroc_get_userdata<wroc_server>(data));
}

// -----------------------------------------------------------------------------

static
void attach(wl_client* client, wl_resource* resource, wl_resource* wl_buffer, i32 x, i32 y)
{
    auto* surface = wroc_get_userdata<wroc_surface>(resource);
    auto* pending = surface->pending;

    pending->buffer.lock = nullptr;
    pending->buffer.handle = wl_buffer ? wroc_get_userdata<wroc_buffer>(wl_buffer) : nullptr;
    pending->committed.insert(wroc_surface_committed_state::buffer);

    if (x || y) {
        if (wl_resource_get_version(resource) >= WL_SURFACE_OFFSET_SINCE_VERSION) {
            wroc_post_error(surface->server, resource, WL_SURFACE_ERROR_INVALID_OFFSET,
                "Non-zero offset not allowed in wl_surface::attach since version {}", WL_SURFACE_OFFSET_SINCE_VERSION);
        } else {
            pending->surface.delta = { x, y };
            pending->committed.insert(wroc_surface_committed_state::offset);
        }
    }
}

wroc_surface_state::~wroc_surface_state()
{
    // TODO: Empty callbacks
}

static
void surface_set_mapped(wroc_surface* surface, bool mapped)
{
    if (mapped == surface->mapped) return;
    surface->mapped = mapped;

    log_info("Surface {} was {}", (void*)surface, mapped ? "mapped" : "unmapped");

    if (mapped) {
        auto* server = surface->server;
        auto* wrui = server->wrui;
        auto* buffer = surface->current.buffer.handle.get();

        surface->window = wrui_window_create(server->client.get());
        wrui_window_set_size(surface->window.get(), buffer->extent);

        auto texture = wrui_texture_create(wrui);
        surface->texture = texture;
        wrui_texture_set_image(texture.get(), buffer->image.get(), server->sampler.get(), wren_blend_mode::premultiplied);
        wrui_texture_set_dst(texture.get(), {{}, buffer->extent, wrei_xywh});

        wrui_node_set_transform(texture.get(), wrui_window_get_transform(surface->window.get()));
        wrui_tree_place_above(wrui_window_get_tree(surface->window.get()), nullptr, texture.get());

        wrui_window_map(surface->window.get());
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
void update_map_state(wroc_surface* surface)
{
    bool can_be_mapped =
           surface->current.buffer.handle
        && surface->current.buffer.handle->image
        && surface->wl_surface;

    surface_set_mapped(surface, can_be_mapped);
}

static
void apply(wroc_surface* surface, wroc_surface_state& from)
{
    surface->current.commit = from.commit;

    WROC_ADDON_SIMPLE_STATE_APPLY(from, surface->current, buffer.transform, buffer_transform);
    WROC_ADDON_SIMPLE_STATE_APPLY(from, surface->current, buffer.scale,     buffer_scale);

    if (from.committed.contains(wroc_surface_committed_state::buffer)) {
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

    wroc_xdg_surface_apply(surface, from);
    wroc_toplevel_apply(   surface, from);
    // wroc_subsurface_apply( surface, from);

    surface->current.committed.insert_range(from.committed);
}

static
void flush(wroc_surface* surface)
{
    // TODO: Queued applications

    auto prev_applied_commit_id = surface->current.commit;

    while (surface->cached.size() > 1) {
        auto& packet = surface->cached.front();

        // TODO: Subsurface parent commit dependencies

        // Check for buffer ready
        if (packet.buffer.lock && !packet.buffer.lock->buffer->is_ready(surface)) {
            wrei_debugkill();
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
        wrui_texture_set_image(surface->texture.get(), surface->current.buffer.handle->image.get(), surface->server->sampler.get(), wren_blend_mode::premultiplied);
    }

    update_map_state(surface);
}

static
void commit(wl_client* client, wl_resource* resource)
{
    auto* surface = wroc_get_userdata<wroc_surface>(resource);

    auto* pending = surface->pending;
    pending->commit = ++surface->last_commit_id;
    surface->pending = &surface->cached.emplace_back();

    // Begin acquisition process for buffers

    if (pending->committed.contains(wroc_surface_committed_state::buffer)) {
        if (pending->buffer.handle) {
            pending->buffer.lock = pending->buffer.handle->commit(surface);
        }
    }

    // Attempt to flush any state immediately

    flush(surface);
}

WROC_INTERFACE(wl_surface) = {
    .destroy = wroc_simple_destroy,
    .attach = attach,
    WROC_STUB(damage),
    WROC_STUB(frame),
    WROC_STUB(set_opaque_region),
    WROC_STUB(set_input_region),
    .commit = commit,
    .set_buffer_transform = WROC_ADDON_SIMPLE_STATE_REQUEST(wroc_surface, buffer.transform, buffer_transform, wl_output_transform(bt), i32 bt),
    .set_buffer_scale     = WROC_ADDON_SIMPLE_STATE_REQUEST(wroc_surface, buffer.scale,     buffer_scale,     scale,                   i32 scale),
    WROC_STUB(damage_buffer),
    WROC_STUB(offset),
};

// -----------------------------------------------------------------------------

wroc_surface::~wroc_surface()
{
}

WROC_NAMESPACE_END
