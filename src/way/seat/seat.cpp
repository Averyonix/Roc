#include "seat.hpp"

#include "../data/data.hpp"
#include "../surface/surface.hpp"
#include "../client.hpp"

// -----------------------------------------------------------------------------

static
auto get_keymap_file(xkb_keymap* keymap) -> WayKeymap
{
    auto string = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
    defer { free(string); };
    u32 size = strlen(string) + 1;

    auto fd = Fd(unix_check<memfd_create>(PROGRAM_NAME "-keymap", MFD_ALLOW_SEALING | MFD_CLOEXEC).value);
    unix_check<ftruncate>(fd.get(), size);

    auto mapped = unix_check<mmap>(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0).value;
    memcpy(mapped, string, size);
    munmap(mapped, size);

    // Seal file to prevent further writes
    unix_check<fcntl>(fd.get(), F_ADD_SEALS, F_SEAL_WRITE | F_SEAL_SHRINK | F_SEAL_GROW);

    return { .fd = std::move(fd), .size = size };
}

static
void init_seat(WayServer* server, Seat* SceneSeat)
{
    auto seat = ref_create<WaySeat>();
    seat->server = server;
    seat->scene = SceneSeat;

    server->seats.emplace_back(seat.get());

    auto& kb_info = seat_keyboard_get_info(seat_get_keyboard(SceneSeat));

    seat->keyboard.keymap = get_keymap_file(kb_info.keymap);

    seat->global = way_global(server, wl_seat, seat.get());

    way_global(server, zwp_relative_pointer_manager_v1);
    way_global(server, zwp_pointer_constraints_v1);
}

WaySeat::~WaySeat()
{
    wl_global_remove(global);
}

void way_seat_init(WayServer* server)
{
    init_seat(server, wm_get_seat(server->wm));
}

// -----------------------------------------------------------------------------

static
void get_keyboard(wl_client* wl_client, wl_resource* resource, u32 id)
{
    auto* seat_client = way_get_userdata<WaySeatClient>(resource);
    auto* seat = seat_client->seat;
    auto* server = seat_client->client->server;

    auto* kb = way_resource_create_refcounted(wl_keyboard, wl_client, resource, id, seat_client);
    seat_client->keyboards.emplace_back(kb);

    way_send(server, wl_keyboard_send_keymap, kb,
        WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
        seat->keyboard.keymap.fd.get(), seat->keyboard.keymap.size);

    auto& kb_info = seat_keyboard_get_info(seat_get_keyboard(seat_client->seat->scene));

    if (wl_resource_get_version(kb) >= WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION) {
        way_send(server, wl_keyboard_send_repeat_info, kb, kb_info.rate, kb_info.delay);
    }
}

WAY_INTERFACE(wl_keyboard) = {
    .release = way_simple_destroy,
};

// -----------------------------------------------------------------------------

static
void get_pointer(wl_client* wl_client, wl_resource* resource, u32 id)
{
    auto* seat_client = way_get_userdata<WaySeatClient>(resource);

    seat_client->pointers.emplace_back(way_resource_create_refcounted(wl_pointer, wl_client, resource, id, seat_client));
}

static
void set_cursor(wl_client* wl_client, wl_resource* resource, u32 serial, wl_resource* wl_surface, int hot_x, int hot_y)
{
    auto* seat_client = way_get_userdata<WaySeatClient>(resource);
    auto* seat = seat_client->seat;
    auto* surface = wl_surface ? way_get_userdata<WaySurface>(wl_surface) : nullptr;

    if (surface) {
        scene_tree_set_translation(surface->scene.tree.get(), {-hot_x, -hot_y});

        if (surface->role != WaySurfaceRole::cursor) {
            surface->role = WaySurfaceRole::cursor;
            scene_node_unparent(surface->scene.input_region.get());
        }
    }

    if (seat->pointer.scene) {
        seat_pointer_set_cursor(seat->pointer.scene, surface ? surface->scene.tree.get() : nullptr);
    }
}

WAY_INTERFACE(wl_pointer) = {
    .set_cursor = set_cursor,
    .release = way_simple_destroy,
};

// -----------------------------------------------------------------------------

WAY_INTERFACE(wl_seat) = {
    .get_pointer = get_pointer,
    .get_keyboard = get_keyboard,
    WAY_STUB(get_touch),
    .release = way_simple_destroy,
};

WAY_BIND_GLOBAL(wl_seat, bind)
{
    auto* seat = way_get_userdata<WaySeat>(bind.data);
    auto* server = seat->server;
    auto* client = way_client_from(server, bind.client);

    auto seat_client = ref_create<WaySeatClient>();
    seat_client->seat = seat;
    seat_client->client = client;

    client->seat_clients.emplace_back(seat_client.get());

    auto* resource = way_resource_create_refcounted(wl_seat, bind.client, bind.version, bind.id, seat_client.get());

    way_send(server, wl_seat_send_capabilities, resource, WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_POINTER);

    if (bind.version >= WL_SEAT_NAME_SINCE_VERSION) {
        way_send(server, wl_seat_send_name, resource, "seat0");
    }

    // TODO: Synchronize with current seat keyboard/pointer/data state

    way_data_offer_selection(seat_client.get());
}

WaySeatClient::~WaySeatClient()
{
    std::erase(client->seat_clients, this);
}

// -----------------------------------------------------------------------------

static
auto find_surface(WayClient* client, SeatInputRegion* region) -> WaySurface*
{
    if (!region) return nullptr;
    if (region->client != client->scene.get()) return nullptr;
    for (auto* surface : client->surfaces) {
        if (surface->scene.input_region.get() == region) return surface;
    }
    return nullptr;
}

static
auto elapsed_ms(WayServer* server) -> u32
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(way_get_elapsed(server)).count();
}

// -----------------------------------------------------------------------------

static
void keyboard_leave(WaySeatClient* seat_client)
{
    auto* seat = seat_client->seat;
    auto* server = seat->server;
    auto* surface = seat->focus.keyboard.get();
    if (!surface) return;

    seat->keyboard.scene = nullptr;
    auto serial = way_next_serial(server);

    if (surface->wl_surface) {
        for (auto* resource : seat_client->keyboards) {
            way_send(server, wl_keyboard_send_leave, resource, serial.value, surface->wl_surface);
        }
    }

    debug_assert(seat->focus.keyboard.get() == surface, "Keyboard left surface that did not have focus");
    seat->focus.keyboard = nullptr;
}

void way_seat_on_keyboard_leave(WaySeatClient* seat_client, SeatEvent* event)
{
    keyboard_leave(seat_client);
}

static
auto find_root_toplevel(WaySurface* surface) -> WaySurface*
{
    if (surface->role == WaySurfaceRole::xdg_toplevel) return surface;
    debug_assert(surface->parent);
    return find_root_toplevel(surface->parent.get());
}

void way_seat_on_keyboard_enter(WaySeatClient* seat_client, SeatEvent* event)
{
    auto* seat = seat_client->seat;
    auto* server = seat->server;

    auto* surface = find_surface(seat_client->client, event->keyboard.focus);

    // xdg_popup and wl_subsurface cannot have keyboard focus
    surface = find_root_toplevel(surface);
    if (surface == seat->focus.keyboard.get()) return;

    // Leave previous window
    keyboard_leave(seat_client);

    if (surface->toplevel.window) {
        wm_window_raise(surface->toplevel.window.get());
    }

    seat->keyboard.scene = event->keyboard.keyboard;

    auto serial = way_next_serial(server);

    if (surface->wl_surface) {
        auto pressed = way_to_wl_array<const u32>(seat_keyboard_get_pressed(seat->keyboard.scene));
        for (auto* resource : seat_client->keyboards) {
            way_send(server, wl_keyboard_send_enter, resource, serial.value, surface->wl_surface, &pressed);
        }
    } else {
        log_error("Keyboard enter failed: wl_surface is destroyed for {}", (void*)surface);
    }

    seat->focus.keyboard = surface;

    way_data_offer_selection(seat_client);
}

// -----------------------------------------------------------------------------

static
auto to_fixed(vec2f32 v) -> Vec<2, wl_fixed_t>
{
    return {wl_fixed_from_double(v.x), wl_fixed_from_double(v.y)};
}

static
auto to_surface_pos(WaySurface* surface, vec2f32 global_pos)
{
    return global_pos - scene_tree_get_position(surface->scene.tree.get());
}

static
void pointer_leave_surface(WaySeatClient* seat_client, WaySurface* surface, WaySerial serial)
{
    auto* seat = seat_client->seat;
    auto* server = seat->server;

    if (!surface->wl_surface) return;
    for (auto* resource : seat_client->pointers) {
        way_send(server, wl_pointer_send_leave, resource, serial.value, surface->wl_surface);
    }
}

void way_seat_on_pointer_leave(WaySeatClient* seat_client, SeatEvent* event)
{
    auto* seat = seat_client->seat;

    if (auto* surface = seat->focus.pointer.get()) {
        pointer_leave_surface(seat_client, surface, way_next_serial(seat->server));
    }

    seat->focus.pointer = nullptr;
    seat->pointer.scene = nullptr;
}

void way_seat_on_pointer_enter(WaySeatClient* seat_client, SeatEvent* event)
{
    auto* seat = seat_client->seat;
    auto* server = seat->server;

    seat->pointer.scene = event->pointer.pointer;

    auto serial = way_next_serial(server);

    auto* old_surface = seat->focus.pointer.get();
    auto* new_surface = find_surface(seat_client->client, event->pointer.focus);

    if (old_surface && new_surface && old_surface != new_surface) {
        pointer_leave_surface(seat_client, old_surface, serial);
    }

    seat->focus.pointer = new_surface;

    if (!new_surface->wl_surface) return;
    auto pos = to_fixed(to_surface_pos(new_surface, seat_pointer_get_position(seat->pointer.scene)));
    for (auto* resource : seat_client->pointers) {
        way_send(server, wl_pointer_send_enter, resource, serial.value, new_surface->wl_surface, pos.x, pos.y);
    }
}

// -----------------------------------------------------------------------------

void way_seat_on_key(WaySeatClient* seat_client, SeatEvent* event)
{
    auto* seat = seat_client->seat;
    auto* server = seat->server;

    auto serial = way_next_serial(server);
    u32 time = elapsed_ms(server);
    auto key = event->keyboard.key;
    auto state = key.pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED;

    for (auto* resource : seat_client->keyboards) {
        way_send(server, wl_keyboard_send_key, resource, serial.value, time, key.code, state);
    }
}

void way_seat_on_modifier(WaySeatClient* seat_client, SeatEvent* event)
{
    auto* seat = seat_client->seat;
    auto* server = seat->server;

    auto serial = way_next_serial(server);
    auto kb = seat_keyboard_get_info(seat->keyboard.scene);

    for (auto* resource : seat_client->keyboards) {
        way_send(server, wl_keyboard_send_modifiers, resource, serial.value,
            xkb_state_serialize_mods(  kb.state, XKB_STATE_MODS_DEPRESSED),
            xkb_state_serialize_mods(  kb.state, XKB_STATE_MODS_LATCHED),
            xkb_state_serialize_mods(  kb.state, XKB_STATE_MODS_LOCKED),
            xkb_state_serialize_layout(kb.state, XKB_STATE_LAYOUT_EFFECTIVE));
    }
}

// -----------------------------------------------------------------------------

static
void pointer_frame(WayServer* server, wl_resource* resource)
{
    if (wl_resource_get_version(resource) >= WL_POINTER_FRAME_SINCE_VERSION) {
        way_send(server, wl_pointer_send_frame, resource);
    }
}

void way_seat_on_motion(WaySeatClient* seat_client, SeatEvent* event)
{
    auto* seat = seat_client->seat;
    auto* server = seat->server;

    auto* surface = seat->focus.pointer.get();
    if (!surface) return;

    auto elapsed = way_get_elapsed(server);
    u64 time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    auto pos = to_fixed(to_surface_pos(surface, seat_pointer_get_position(seat->pointer.scene)));

    {
        u64 time_us = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
        u32 time_us_hi = time_us >> 32;
        u32 time_us_lo = time_us & ~0u;

        auto accel   = to_fixed(event->pointer.motion.rel_accel);
        auto unaccel = to_fixed(event->pointer.motion.rel_unaccel);
        for (auto* resource : seat_client->relative_pointers) {
            way_send(server, zwp_relative_pointer_v1_send_relative_motion,
                resource, time_us_hi, time_us_lo, accel.x, accel.y, unaccel.x, unaccel.y);
        }
    }

    for (auto* resource : seat_client->pointers) {
        way_send(server, wl_pointer_send_motion, resource, time_ms, pos.x, pos.y);
        pointer_frame(server, resource);
    }
}

void way_seat_on_button(WaySeatClient* seat_client, SeatEvent* event)
{
    auto* seat = seat_client->seat;
    auto* server = seat->server;

    auto serial = way_next_serial(server);
    u32 time = elapsed_ms(server);
    auto button = event->pointer.button;
    auto state = button.pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED;

    for (auto* resource : seat_client->pointers) {
        way_send(server, wl_pointer_send_button, resource, serial.value, time, button.code, state);
        pointer_frame(server, resource);
    }
}

void way_seat_on_scroll(WaySeatClient* seat_client, SeatEvent* event)
{
    auto* seat = seat_client->seat;
    auto* server = seat->server;

    u32 time = elapsed_ms(server);
    auto delta = event->pointer.scroll.delta;

    static constexpr f32 axis_pixel_rate = 15.f;

    for (auto* resource : seat_client->pointers) {
        auto version = wl_resource_get_version(resource);

        if (version >= WL_POINTER_AXIS_SOURCE_SINCE_VERSION) {
            way_send(server, wl_pointer_send_axis_source, resource, WL_POINTER_AXIS_SOURCE_WHEEL);
        }

        if (delta.x) way_send(server, wl_pointer_send_axis, resource, time, WL_POINTER_AXIS_HORIZONTAL_SCROLL, wl_fixed_from_double(delta.x * axis_pixel_rate));
        if (delta.y) way_send(server, wl_pointer_send_axis, resource, time, WL_POINTER_AXIS_VERTICAL_SCROLL,   wl_fixed_from_double(delta.y * axis_pixel_rate));

        if (version >= WL_POINTER_AXIS_VALUE120_SINCE_VERSION) {
            if (delta.x) way_send(server, wl_pointer_send_axis_value120, resource, WL_POINTER_AXIS_HORIZONTAL_SCROLL, i32(delta.x * 120));
            if (delta.y) way_send(server, wl_pointer_send_axis_value120, resource, WL_POINTER_AXIS_VERTICAL_SCROLL,   i32(delta.y * 120));

        } else if (version >= WL_POINTER_AXIS_DISCRETE_SINCE_VERSION) {
            // TODO: Accumulate fractional values
            if (delta.x) way_send(server, wl_pointer_send_axis_discrete, resource, WL_POINTER_AXIS_HORIZONTAL_SCROLL, i32(delta.x));
            if (delta.y) way_send(server, wl_pointer_send_axis_discrete, resource, WL_POINTER_AXIS_VERTICAL_SCROLL,   i32(delta.y));
        }

        pointer_frame(server, resource);
    }
}
