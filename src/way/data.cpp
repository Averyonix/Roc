#include "internal.hpp"

static
void create_data_source(wl_client* client, wl_resource* resource, u32 id)
{
    way_resource_create_unsafe(wl_data_source, client, resource, id, nullptr);
}

static
void get_data_device(wl_client* client, wl_resource* resource, u32 id, wl_resource* seat)
{
    way_resource_create_unsafe(wl_data_device, client, resource, id, nullptr);
}

WAY_INTERFACE(wl_data_device_manager) = {
    .create_data_source = create_data_source,
    .get_data_device = get_data_device,
};

WAY_BIND_GLOBAL(wl_data_device_manager)
{
    way_resource_create_unsafe(wl_data_device_manager, client, version, id, nullptr);
}

// -----------------------------------------------------------------------------

WAY_INTERFACE(wl_data_offer) = {
    WAY_STUB(accept),
    WAY_STUB(receive),
    .destroy = way_simple_destroy,
    WAY_STUB(finish),
    WAY_STUB(set_actions),
};

// -----------------------------------------------------------------------------

WAY_INTERFACE(wl_data_source) = {
    WAY_STUB(offer),
    .destroy = way_simple_destroy,
    WAY_STUB(set_actions),
};

// -----------------------------------------------------------------------------

WAY_INTERFACE(wl_data_device) = {
    WAY_STUB(start_drag),
    WAY_STUB(set_selection),
    .release = way_simple_destroy,
};
