#include "server.hpp"

static
void wroc_wl_data_device_manager_create_data_source(wl_client* client, wl_resource* resource, u32 id)
{
    auto* new_resource = wl_resource_create(client, &wl_data_source_interface, wl_resource_get_version(resource), id);
    auto* data_source = new wroc_data_source {};
    data_source->resource = new_resource;
    data_source->server = wroc_get_userdata<wroc_server>(resource);
    data_source->server->data_manager.sources.emplace_back(data_source);
    wroc_resource_set_implementation_refcounted(new_resource, &wroc_wl_data_source_impl, data_source);
}

static
void wroc_wl_data_device_manager_get_data_device(wl_client* client, wl_resource* resource, u32 id, wl_resource* seat)
{
    auto* new_resource = wl_resource_create(client, &wl_data_device_interface, wl_resource_get_version(resource), id);
    auto* data_device = new wroc_data_device {};
    data_device->resource = new_resource;
    data_device->server = wroc_get_userdata<wroc_server>(resource);
    data_device->seat = wroc_get_userdata<wroc_seat>(seat);
    data_device->server->data_manager.devices.emplace_back(data_device);
    wroc_resource_set_implementation_refcounted(new_resource, &wroc_wl_data_device_impl, data_device);
}

const struct wl_data_device_manager_interface wroc_wl_data_device_manager_impl = {
    .create_data_source = wroc_wl_data_device_manager_create_data_source,
    .get_data_device = wroc_wl_data_device_manager_get_data_device,
};

void wroc_wl_data_device_manager_bind_global(wl_client* client, void* data, u32 version, u32 id)
{
    auto new_resource = wl_resource_create(client, &wl_data_device_manager_interface, version, id);
    wroc_debug_track_resource(new_resource);
    wroc_resource_set_implementation(new_resource, &wroc_wl_data_device_manager_impl, static_cast<wroc_server*>(data));
}

// -----------------------------------------------------------------------------

static
void wroc_wl_data_offer_receive(wl_client* client, wl_resource* resource, const char* mime_type, int fd)
{
    log_warn("wl_data_offer::receieve(mime = \"{}\", fd = {})", mime_type, fd);

    auto* offer = wroc_get_userdata<wroc_data_offer>(resource);

    if (offer->source) {
        wl_data_source_send_send(offer->source->resource, mime_type, fd);
    } else {
        log_error("Data offer receieve failed: source {} was destroyed", (void*)offer->source.get());
    }

    close(fd);
}

static
void wroc_wl_data_offer_set_actions(wl_client* client, wl_resource* resource, u32 dnd_actions, u32 preferred_action)
{
    auto* data_offer = wroc_get_userdata<wroc_data_offer>(resource);

    if (!data_offer->source) {
        log_warn("wl_data_offer::set_actions failed: source {} was destroyed", (void*)data_offer->source.get());
    }

    log_warn("wl_data_offer::set_actions()");
    if (auto a = dnd_actions & WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY) log_warn(" - copy{}", a == preferred_action ? " *" : "");
    if (auto a = dnd_actions & WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE) log_warn(" - move{}", a == preferred_action ? " *" : "");
    if (auto a = dnd_actions & WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK)  log_warn(" - ask{}", a == preferred_action ? " *" : "");

    if (data_offer->source->dnd_actions >= wl_data_device_manager_dnd_action(preferred_action)) {
        data_offer->action = wl_data_device_manager_dnd_action(preferred_action);
        log_warn("  selecting preferred action");
    } else {
        auto matched = data_offer->source->dnd_actions & wl_data_device_manager_dnd_action(preferred_action);
        log_warn("  matching: {:#b}", u32(matched));
        if (matched) {
            matched = matched & wl_data_device_manager_dnd_action(1u << std::countr_zero(u32(matched)));
        }
        log_warn("  selected: {:#b}", u32(matched));
        data_offer->action = matched;
    }

    wl_data_offer_send_action(data_offer->resource, data_offer->action);
}

static
void wroc_wl_data_offer_accept(wl_client* client, wl_resource* resource, u32 serial, const char* mime_type)
{
    // TODO: Validate serial

    log_warn("wl_data_offer::accept({})", mime_type ? mime_type : "null");

    auto* offer = wroc_get_userdata<wroc_data_offer>(resource);

    if (!offer->source) {
        log_warn("wl_data_offer::accept failed: source {} was destroyed", (void*)offer->source.get());
        return;
    }

    auto* server = offer->server;
    auto& drag = server->data_manager.drag;

    if (offer->source.get() != drag.source.get()) {
        log_warn("  not active drag, ignoring...");
        return;
    }

    bool matches = true;
    if (!mime_type) {
        matches = false;
    } else if (std::ranges::find_if(offer->source->mime_types, [&](auto& t) { return t == mime_type; }) == offer->source->mime_types.end()) {
        log_warn("wl_data_offer::accept - mime type \"{}\" not supported", mime_type);
        matches = false;
    }

    if (matches) {
        log_warn("Offer added to drag");
        drag.offer = offer;
        offer->mime_type = mime_type;
        if (drag.source && drag.source->resource) {
            wl_data_source_send_target(drag.source->resource, mime_type);
        }
    } else if (drag.offer.get() == offer) {
        log_warn("Offer removed from drag");
        drag.offer = nullptr;
        if (drag.source && drag.source->resource) {
            wl_data_source_send_target(drag.source->resource, nullptr);
        }
    }
}

static
void wroc_data_manager_end_grab(wroc_server* server)
{
    server->data_manager.drag = {};
}

static
void wroc_wl_data_offer_finish(wl_client* client, wl_resource* resource)
{
    log_warn("wl_data_offer::finish()");

    auto* offer = wroc_get_userdata<wroc_data_offer>(resource);

    if (offer->source) {
        wl_data_source_send_dnd_finished(offer->source->resource);
    }

    wroc_data_manager_end_grab(offer->server);
}

const struct wl_data_offer_interface wroc_wl_data_offer_impl = {
    .accept      = wroc_wl_data_offer_accept,
    .receive     = wroc_wl_data_offer_receive,
    .destroy     = wroc_simple_resource_destroy_callback,
    .finish      = wroc_wl_data_offer_finish,
    .set_actions = wroc_wl_data_offer_set_actions,
};


wroc_data_offer::~wroc_data_offer()
{
    log_warn("wl_data_offer::destroyed");
}

// -----------------------------------------------------------------------------

static
void wroc_wl_data_source_offer(wl_client* client, wl_resource* resource, const char* mime_type)
{
    log_warn("wl_data_source::offer({})", mime_type);
    auto* data_source = wroc_get_userdata<wroc_data_source>(resource);
    data_source->mime_types.emplace_back(mime_type);
}

static
void wroc_wl_data_source_set_actions(wl_client* client, wl_resource* resource, u32 dnd_actions)
{
    log_warn("wl_data_source::set_actions()");
    if (dnd_actions >= WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY) log_warn(" - copy");
    if (dnd_actions >= WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE) log_warn(" - move");
    if (dnd_actions >= WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK)  log_warn(" - ask");

    auto* data_source = wroc_get_userdata<wroc_data_source>(resource);
    data_source->dnd_actions = wl_data_device_manager_dnd_action(dnd_actions);
}

static
void wroc_wl_data_source_destroy(wl_client* client, wl_resource* resource)
{
    auto* source = wroc_get_userdata<wroc_data_source>(resource);
    log_debug("data source destroyed: {}", (void*)source);
    if (source->server->data_manager.drag.source.get() == source) {
        wroc_data_manager_end_grab(source->server);
    }
}

const struct wl_data_source_interface wroc_wl_data_source_impl = {
    .offer       = wroc_wl_data_source_offer,
    .destroy     = wroc_wl_data_source_destroy,
    .set_actions = wroc_wl_data_source_set_actions,
};

wroc_data_source::~wroc_data_source()
{
    log_warn("wroc_data_source::destroyed");
    std::erase(server->data_manager.sources, this);
}

static
void wroc_data_source_cancel(wroc_data_source* data_source)
{
    if (!data_source) return;
    if (data_source->cancelled) return;
    log_warn("Cancelling data source: {}", (void*)data_source);
    if (data_source->resource) {
        wl_data_source_send_cancelled(data_source->resource);
    }
    data_source->cancelled = true;
    if (data_source == data_source->server->data_manager.drag.source.get()) {
        wroc_data_manager_end_grab(data_source->server);
    }
}

// -----------------------------------------------------------------------------

static
wl_resource* wroc_data_device_offer(wroc_data_device* device, wroc_data_source* source)
{
    auto* offer_resource = wl_resource_create(wroc_resource_get_client(device->resource), &wl_data_offer_interface, wl_resource_get_version(device->resource), 0);
    auto* data_offer = new wroc_data_offer {};
    data_offer->resource = offer_resource;
    data_offer->server = device->server;
    data_offer->source = source;
    data_offer->device = device;
    wroc_resource_set_implementation_refcounted(offer_resource, &wroc_wl_data_offer_impl, data_offer);

    wl_data_device_send_data_offer(device->resource, offer_resource);

    for (auto& mime_type : source->mime_types) {
        wl_data_offer_send_offer(offer_resource, mime_type.c_str());
    }

    if (wl_resource_get_version(offer_resource) >= WL_DATA_OFFER_ACTION_SINCE_VERSION) {
        wl_data_offer_send_source_actions(offer_resource, source->dnd_actions);
    }

    return offer_resource;
}

void wroc_data_manager_offer_selection(wroc_server* server, wl_client* client)
{
    if (!server->data_manager.selection) return;
    auto* selection = server->data_manager.selection.get();
    for (auto* device : server->data_manager.devices) {
        if (wroc_resource_get_client(device->resource) != client) continue;
        // TODO: We should also be filtering per seat when/if we support multiple of those
        log_warn("Offering selection {} to {}", (void*)selection, (void*)device);
        auto* offer_resource = wroc_data_device_offer(device, selection);
        wl_data_device_send_selection(device->resource, offer_resource);
    }
}

static
void wroc_wl_data_device_set_selection(wl_client* client, wl_resource* resource, wl_resource* source, u32 serial)
{
    // TODO: Validate serial

    auto* data_source = wroc_get_userdata<wroc_data_source>(source);

    // Cancel all previous data sources

    for (auto* old_source : data_source->server->data_manager.sources) {
        if (old_source == data_source) continue;
        wroc_data_source_cancel(old_source);
    }

    data_source->server->data_manager.selection = data_source;

    log_warn("wl_data_device::set_selection({})", (void*)data_source);

    if (auto* surface = data_source->server->seat->keyboard->focused_surface.get(); surface && surface->resource) {
        if (auto* focused_client = wroc_resource_get_client(surface->resource)) {
            wroc_data_manager_offer_selection(data_source->server, focused_client);
        }
    }
}

void wroc_data_manager_update_drag(wroc_server* server, wroc_surface* target_surface)
{
    auto& drag = server->data_manager.drag;

    if (!drag.source || !drag.device) return;

    if (drag.offered_surface.get() == target_surface) {
        if (!target_surface) return;

        auto time = wroc_get_elapsed_milliseconds(server);

        for (auto* device : server->data_manager.devices) {
            if (wroc_resource_get_client(device->resource) != wroc_resource_get_client(target_surface->resource)) continue;

            if (wroc_is_client_behind(wroc_resource_get_client(device->resource))) {
                log_warn("[{}], client is running behind, skipping drag motion event...", time);
                continue;
            }

            auto pos = server->seat->pointer->layout_position - vec2f64(target_surface->position);
            log_warn("Drag moved in {} - ({}, {}) [{}]", (void*)target_surface, pos.x, pos.y, time);
            wl_data_device_send_motion(device->resource,
                time,
                wl_fixed_from_double(pos.x),
                wl_fixed_from_double(pos.y));
        }
        return;
    }

    if (drag.offered_surface) {
        for (auto* device : server->data_manager.devices) {
            if (wroc_resource_get_client(device->resource) != wroc_resource_get_client(drag.offered_surface->resource)) continue;

            log_warn("Drag left: {}", (void*)target_surface);
            wl_data_device_send_leave(device->resource);
        }
    }

    drag.offered_surface = target_surface;
    drag.offer = nullptr;
    if (target_surface) {
        for (auto* device : server->data_manager.devices) {
            if (wroc_resource_get_client(device->resource) != wroc_resource_get_client(target_surface->resource)) continue;

            auto pos = server->seat->pointer->layout_position - vec2f64(target_surface->position);
            log_warn("Drag entered {} at ({}, {})", (void*)target_surface, pos.x, pos.y);
            auto* offer_resource = wroc_data_device_offer(device, drag.source.get());
            wl_data_device_send_enter(device->resource,
                wl_display_next_serial(server->display),
                target_surface->resource,
                wl_fixed_from_double(pos.x),
                wl_fixed_from_double(pos.y),
                offer_resource);
        }
    }
}

void wroc_data_manager_finish_drag(wroc_server* server)
{
    auto& drag = server->data_manager.drag;

    if (!drag.source || !drag.device) {
        drag = {};
        return;
    }



    if (drag.offer && drag.offer->device && drag.offer->source) {
        if (drag.offer->action && drag.source->dnd_actions >= drag.offer->action) {
            log_warn("Drag completed with offer");
            log_warn("  action = {}", magic_enum::enum_name(drag.offer->action));
            log_warn("  mime_type = {}", drag.offer->mime_type);
            wl_data_device_send_drop(drag.offer->device->resource);
            wl_data_source_send_dnd_drop_performed(drag.source->resource);
        } else {
            log_warn("Drag completed with no matching action, cancelling");
            wroc_data_source_cancel(drag.source.get());
        }
    } else {
        log_warn("Drag completed with no offered device (or source was destroyed), cancelling");
        wroc_data_source_cancel(drag.source.get());
    }
}

static
void wroc_wl_data_device_start_drag(wl_client* client, wl_resource* resource, wl_resource* source,
                                    wl_resource* origin, wl_resource* icon, u32 serial)
{
    // TODO: Validate serial

    auto* data_device = wroc_get_userdata<wroc_data_device>(resource);
    // TODO: Handle drag with null source
    auto* data_source = wroc_get_userdata<wroc_data_source>(source);
    // auto* origin_surface = wroc_get_userdata<wroc_surface>(origin);
    auto* drag_icon = wroc_get_userdata<wroc_surface>(icon);

    auto* server = data_device->server;

    log_warn("Drag started (device = {}, source = {}, icon = {})", (void*)data_device, (void*)data_source, (void*)drag_icon);

    server->data_manager.drag.device = data_device;
    server->data_manager.drag.source = data_source;
    server->data_manager.drag.icon = drag_icon;
    server->data_manager.drag.offered_surface = nullptr;
    server->data_manager.drag.offer = nullptr;

    wroc_data_manager_update_drag(server, server->surface_under_cursor.get());
}

const struct wl_data_device_interface wroc_wl_data_device_impl = {
    .start_drag    = wroc_wl_data_device_start_drag,
    .set_selection = wroc_wl_data_device_set_selection,
    .release       = wroc_simple_resource_destroy_callback,
};

wroc_data_device::~wroc_data_device()
{
    std::erase(server->data_manager.devices, this);
}
