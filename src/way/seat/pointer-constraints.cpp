#include "seat.hpp"
#include "../server.hpp"

static
void constrain_pointer(
    bool locked,
    wl_client* client,
    wl_resource* resource,
    u32 id,
    wl_resource* wl_surface,
    wl_resource* wL_pointer,
    wl_resource* wl_region,
    u32 _lifetime)
{
    auto lifetime = zwp_pointer_constraints_v1_lifetime(_lifetime);

    log_error("TODO: {} pointer - {}", locked ? "lock" : "confine", lifetime);

    if (locked) {
        way_resource_create_unsafe(zwp_locked_pointer_v1, client, resource, id, nullptr);
    } else {
        way_resource_create_unsafe(zwp_confined_pointer_v1, client, resource, id, nullptr);
    }
}

static
void lock_pointer(auto ...args)
{
    constrain_pointer(true, args...);
}

static
void confine_pointer(auto ...args)
{
    constrain_pointer(false, args...);
}

WAY_INTERFACE(zwp_pointer_constraints_v1) = {
    .destroy = way_simple_destroy,
    .lock_pointer = lock_pointer,
    .confine_pointer = confine_pointer,
};

WAY_BIND_GLOBAL(zwp_pointer_constraints_v1, bind)
{
    way_resource_create_unsafe(zwp_pointer_constraints_v1, bind.client, bind.version, bind.id, way_get_userdata<WayServer>(bind.data));
}

// -----------------------------------------------------------------------------

WAY_INTERFACE(zwp_locked_pointer_v1) = {
    .destroy = way_simple_destroy,
    WAY_STUB(set_cursor_position_hint),
    WAY_STUB(set_region),
};

WAY_INTERFACE(zwp_confined_pointer_v1) = {
    .destroy = way_simple_destroy,
    WAY_STUB(set_region),
};
