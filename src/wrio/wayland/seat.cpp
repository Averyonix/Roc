#include "wayland.hpp"

static
void set_keyboard(wrio_context* ctx)
{
    ctx->wayland->keyboard = wrei_create<wrio_input_device_wayland_keyboard>();
}

// -----------------------------------------------------------------------------

static
void set_pointer(wrio_context* ctx)
{
    ctx->wayland->pointer = wrei_create<wrio_input_device_wayland_pointer>();
}

// -----------------------------------------------------------------------------

static
void seat_capabilities(void* udata, wl_seat*, u32 capabilities)
{
    auto* ctx = static_cast<wrio_context*>(udata);

    if      ( (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !ctx->wayland->keyboard) set_keyboard(ctx);
    else if (!(capabilities & WL_SEAT_CAPABILITY_KEYBOARD))    ctx->wayland->keyboard = nullptr;

    if      ( (capabilities & WL_SEAT_CAPABILITY_POINTER) && !ctx->wayland->pointer) set_pointer(ctx);
    else if (!(capabilities & WL_SEAT_CAPABILITY_POINTER))    ctx->wayland->pointer = nullptr;
}

WRIO_WL_LISTENER(wl_seat) = {
    .capabilities = seat_capabilities,
    WRIO_WL_STUB(wl_seat, name),
};
