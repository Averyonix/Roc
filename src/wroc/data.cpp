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

    auto* source = wroc_get_userdata<wroc_data_source>(resource);

    if (source->resource) {
        wl_data_source_send_send(source->resource, mime_type, fd);
    }

    close(fd);
}

const struct wl_data_offer_interface wroc_wl_data_offer_impl = {
    .accept      = WROC_NOISY_STUB(accept),
    .receive     = wroc_wl_data_offer_receive,
    .destroy     = wroc_simple_resource_destroy_callback,
    .finish      = WROC_NOISY_STUB(finish),
    .set_actions = WROC_NOISY_STUB(set_actions),
};

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

const struct wl_data_source_interface wroc_wl_data_source_impl = {
    .offer       = wroc_wl_data_source_offer,
    .destroy     = wroc_simple_resource_destroy_callback,
    .set_actions = wroc_wl_data_source_set_actions,
};

wroc_data_source::~wroc_data_source()
{
    std::erase(server->data_manager.sources, this);
}

static
void wroc_data_source_cancel(wroc_data_source* data_source)
{
    if (data_source->cancelled) return;
    log_warn("Cancelling data source: {}", (void*)data_source);
    if (data_source->resource) {
        wl_data_source_send_cancelled(data_source->resource);
    }
    data_source->cancelled = true;
}

// -----------------------------------------------------------------------------

static
void wroc_data_device_offer(wroc_data_device* device, wroc_data_source* source, bool selection)
{
    auto* offer_resource = wl_resource_create(wl_resource_get_client(device->resource), &wl_data_offer_interface, wl_resource_get_version(device->resource), 0);
    wrei_add_ref(source);
    source->offers.emplace_back(offer_resource);
    wroc_resource_set_implementation_refcounted(offer_resource, &wroc_wl_data_offer_impl, source);

    wl_data_device_send_data_offer(device->resource, offer_resource);

    for (auto& mime_type : source->mime_types) {
        wl_data_offer_send_offer(offer_resource, mime_type.c_str());
    }

    if (wl_resource_get_version(offer_resource) >= WL_DATA_OFFER_ACTION_SINCE_VERSION) {
        wl_data_offer_send_source_actions(offer_resource, source->dnd_actions);
    }

    if (selection) {
        wl_data_device_send_selection(device->resource, offer_resource);
    }
}

void wroc_data_manager_offer_selection(wroc_server* server, wl_client* client)
{
    if (!server->data_manager.selection) return;
    auto* selection = server->data_manager.selection.get();
    for (auto* device : server->data_manager.devices) {
        if (wl_resource_get_client(device->resource) != client) continue;
        // TODO: We should also be filtering per seat when/if we support multiple of those
        log_warn("Offering selection {} to {}", (void*)selection, (void*)device);
        wroc_data_device_offer(device, selection, true);
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

    data_source->server->data_manager.selection = wrei_weak_from(data_source);

    log_warn("wl_data_device::set_selection({})", (void*)data_source);

    if (auto* surface = data_source->server->seat->keyboard->focused_surface.get(); surface && surface->resource) {
        if (auto* focused_client = wl_resource_get_client(surface->resource)) {
            wroc_data_manager_offer_selection(data_source->server, focused_client);
        }
    }
}

const struct wl_data_device_interface wroc_wl_data_device_impl = {
    .start_drag    = WROC_NOISY_STUB(start_drag),
    .set_selection = wroc_wl_data_device_set_selection,
    .release       = wroc_simple_resource_destroy_callback,
};

wroc_data_device::~wroc_data_device()
{
    std::erase(server->data_manager.devices, this);
}
