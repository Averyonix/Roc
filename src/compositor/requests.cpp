#include "server.hpp"
#include "compositor/protocol/wayland_server.hpp"

using namespace wayland::server;

// ---- wl_display -------------------------------------------------------------

void wl_display::sync(Client*, wl_callback* callback)
{
    log_warn("wl_display::sync(callback = {})", callback->_client_ids[0].id);

    callback->done({}, 0);
}

void wl_display::get_registry(Client* client, wl_registry* registry)
{
    log_warn("wl_display::get_registry(registry = {})", registry->_client_ids[0].id);

    auto compositor = new wl_compositor(display_from_client(client));
    registry->global({}, compositor->_name, wl_compositor::InterfaceName, wl_compositor::Version);

    auto shm = new wl_shm(display_from_client(client));
    registry->global({}, shm->_name, wl_shm::InterfaceName, wl_compositor::Version);

    auto xdg_wm_base = new server::xdg_wm_base(display_from_client(client));
    registry->global({}, xdg_wm_base->_name, server::xdg_wm_base::InterfaceName, server::xdg_wm_base::Version);
}

// ---- wl_registry ------------------------------------------------------------

void wl_registry::bind(Client*, u32 name, NewId id)
{
    log_warn("wl_registry::bind(interface = {}, version = {}, new_id = {})", id.interface, id.version, id.new_id);
}

// ---- wl_output --------------------------------------------------------------

// ---- wl_surface -------------------------------------------------------------

void wl_surface::destroy(Client*) {}
void wl_surface::attach(Client*, wl_buffer* buffer, i32 x, i32 y) {}
void wl_surface::damage(Client*, i32 x, i32 y, i32 width, i32 height) {}
void wl_surface::frame(Client*, wl_callback* callback) {}
void wl_surface::set_opaque_region(Client*, wl_region* region) {}
void wl_surface::set_input_region(Client*, wl_region* region) {}
void wl_surface::commit(Client*) {}
void wl_surface::set_buffer_transform(Client*, wl_output_transform transform) {}
void wl_surface::set_buffer_scale(Client*, i32 scale) {}
void wl_surface::damage_buffer(Client*, i32 x, i32 y, i32 width, i32 height) {}
void wl_surface::offset(Client*, i32 x, i32 y) {}

// ---- wl_buffer --------------------------------------------------------------

void wl_buffer::destroy(Client*) {}

// ---- wl_compositor ----------------------------------------------------------

void wl_compositor::create_surface(Client*, wl_surface* id) {}
void wl_compositor::create_region(Client*, wl_region* id) {}

// ---- wl_shm -----------------------------------------------------------------

void wl_shm::create_pool(Client*, wl_shm_pool* id, int fd, i32 size) { }
void wl_shm::release(Client*) { }

// ---- wl_shm_pool ------------------------------------------------------------

void wl_shm_pool::create_buffer(Client*, wl_buffer* id, i32 offset, i32 width, i32 height, i32 stride, wl_shm_format format) { }
void wl_shm_pool::destroy(Client*) { }
void wl_shm_pool::resize(Client*, i32 size) { }

// ---- wl_seat ----------------------------------------------------------------

// ---- xdg_surface ------------------------------------------------------------#

void xdg_surface::destroy(Client*) {}
void xdg_surface::get_toplevel(Client*, xdg_toplevel* id) {}
void xdg_surface::get_popup(Client*, xdg_popup* id, xdg_surface* parent, xdg_positioner* positioner) {}
void xdg_surface::set_window_geometry(Client*, i32 x, i32 y, i32 width, i32 height) {}
void xdg_surface::ack_configure(Client*, u32 serial) {}

// ---- xdg_wm_base ------------------------------------------------------------

void xdg_wm_base::destroy(Client*) {}
void xdg_wm_base::create_positioner(Client*, xdg_positioner* id) {}
void xdg_wm_base::get_xdg_surface(Client*, xdg_surface* id, wl_surface* surface) {}
void xdg_wm_base::pong(Client*, u32 serial) {}

// ---- xdg_toplevel -----------------------------------------------------------

void xdg_toplevel::destroy(Client*) {}
void xdg_toplevel::set_parent(Client*, xdg_toplevel* parent) {}
void xdg_toplevel::set_title(Client*, std::string_view title) {}
void xdg_toplevel::set_app_id(Client*, std::string_view app_id) {}
void xdg_toplevel::show_window_menu(Client*, wl_seat* seat, u32 serial, i32 x, i32 y) {}
void xdg_toplevel::move(Client*, wl_seat* seat, u32 serial) {}
void xdg_toplevel::resize(Client*, wl_seat* seat, u32 serial, xdg_toplevel_resize_edge edges) {}
void xdg_toplevel::set_max_size(Client*, i32 width, i32 height) {}
void xdg_toplevel::set_min_size(Client*, i32 width, i32 height) {}
void xdg_toplevel::set_maximized(Client*) {}
void xdg_toplevel::unset_maximized(Client*) {}
void xdg_toplevel::set_fullscreen(Client*, wl_output* output) {}
void xdg_toplevel::unset_fullscreen(Client*) {}
void xdg_toplevel::set_minimized(Client*) {}
