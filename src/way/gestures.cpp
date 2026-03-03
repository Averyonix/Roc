#include "internal.hpp"

static
void get_swipe_gesture(wl_client* client, wl_resource* resource, u32 id, wl_resource* pointer)
{
    way_resource_create_unsafe(zwp_pointer_gesture_swipe_v1, client,resource, id, nullptr);
}

static
void get_pinch_gesture(wl_client* client, wl_resource* resource, u32 id, wl_resource* pointer)
{
    way_resource_create_unsafe(zwp_pointer_gesture_pinch_v1, client,resource, id, nullptr);
}

static
void get_hold_gesture(wl_client* client, wl_resource* resource, u32 id, wl_resource* pointer)
{
    way_resource_create_unsafe(zwp_pointer_gesture_hold_v1, client,resource, id, nullptr);
}

WAY_INTERFACE(zwp_pointer_gestures_v1) = {
    .get_swipe_gesture = get_swipe_gesture,
    .get_pinch_gesture = get_pinch_gesture,
    .release = way_simple_destroy,
    .get_hold_gesture = get_hold_gesture,
};

WAY_BIND_GLOBAL(zwp_pointer_gestures_v1)
{
    way_resource_create_unsafe(zwp_pointer_gestures_v1, client, version, id, nullptr);
}

// -----------------------------------------------------------------------------

WAY_INTERFACE(zwp_pointer_gesture_swipe_v1) = {
    .destroy = way_simple_destroy,
};

// -----------------------------------------------------------------------------

WAY_INTERFACE(zwp_pointer_gesture_pinch_v1) = {
    .destroy = way_simple_destroy,
};

// -----------------------------------------------------------------------------

WAY_INTERFACE(zwp_pointer_gesture_hold_v1) = {
    .destroy = way_simple_destroy,
};
