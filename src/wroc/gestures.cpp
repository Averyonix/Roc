#include "protocol.hpp"

const u32 wroc_zwp_pointer_gestures_v1_version = 3;

static
void wroc_pointer_gestures_get_swipe_gesture(wl_client* client, wl_resource* resource, u32 id, wl_resource* pointer)
{
    auto dummy = wroc_resource_create(client, &zwp_pointer_gesture_swipe_v1_interface, wl_resource_get_version(resource), id);
    wroc_resource_set_implementation(dummy, &wroc_zwp_pointer_gesture_swipe_v1_impl, nullptr);
}

static
void wroc_pointer_gestures_get_pinch_gesture(wl_client* client, wl_resource* resource, u32 id, wl_resource* pointer)
{
    auto dummy = wroc_resource_create(client, &zwp_pointer_gesture_pinch_v1_interface, wl_resource_get_version(resource), id);
    wroc_resource_set_implementation(dummy, &wroc_zwp_pointer_gesture_pinch_v1_impl, nullptr);
}

static
void wroc_pointer_gestures_get_hold_gesture(wl_client* client, wl_resource* resource, u32 id, wl_resource* pointer)
{
    auto dummy = wroc_resource_create(client, &zwp_pointer_gesture_hold_v1_interface, wl_resource_get_version(resource), id);
    wroc_resource_set_implementation(dummy, &wroc_zwp_pointer_gesture_hold_v1_impl, nullptr);
}

const struct zwp_pointer_gestures_v1_interface wroc_zwp_pointer_gestures_v1_impl = {

    .get_swipe_gesture = wroc_pointer_gestures_get_swipe_gesture,
    .get_pinch_gesture = wroc_pointer_gestures_get_pinch_gesture,
    .release = wroc_simple_resource_destroy_callback,
    .get_hold_gesture = wroc_pointer_gestures_get_hold_gesture,
};

void wroc_zwp_pointer_gestures_v1_bind_global(wl_client* client, void* data, u32 version, u32 id)
{
    auto dummy = wroc_resource_create(client, &zwp_pointer_gestures_v1_interface, version, id);
    wroc_resource_set_implementation(dummy, &wroc_zwp_pointer_gestures_v1_impl, nullptr);
}

const struct zwp_pointer_gesture_swipe_v1_interface wroc_zwp_pointer_gesture_swipe_v1_impl = {
    .destroy = wroc_simple_resource_destroy_callback,
};

const struct zwp_pointer_gesture_pinch_v1_interface wroc_zwp_pointer_gesture_pinch_v1_impl = {
    .destroy = wroc_simple_resource_destroy_callback,
};

const struct zwp_pointer_gesture_hold_v1_interface wroc_zwp_pointer_gesture_hold_v1_impl = {
    .destroy = wroc_simple_resource_destroy_callback,
};
