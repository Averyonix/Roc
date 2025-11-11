#include "server.hpp"

#include "protocol/protocol.hpp"

void keyboard_added(Keyboard* kb)
{
    log_error("KEYBOARD ADDED");

    kb->server->seat->keyboard = kb;
};

static
std::string random_file_name()
{
    static std::mt19937 rng{std::default_random_engine{}()};
    static std::uniform_int_distribution<u64> dist{0, UINT64_MAX};
    return std::format("/" PROGRAM_NAME "-{}", dist(rng));
}

static
int excl_shm_open(std::string& name)
{
    for (int i = 0; i < 100; ++i) {
        name = random_file_name();
        int fd = unix_check_n1(shm_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600), EEXIST);
        if (fd >= 0) return fd;
        if (errno != EEXIST) break;
    }

    return -1;
}

bool allocate_shm_file_pair(usz size, int* p_rw_fd, int* p_ro_fd)
{
    std::string name;
    int rw_fd = excl_shm_open(name);
    if (rw_fd < 0) {
        return false;
    }

    int ro_fd = unix_check_n1(shm_open(name.c_str(), O_RDONLY, 0));
    if (ro_fd < 0) {
        shm_unlink(name.c_str());
        close(rw_fd);
        return false;
    }

    shm_unlink(name.c_str());

    if (fchmod(rw_fd, 0) != 0) {
        close(rw_fd);
        close(ro_fd);
        log_unix_error("allocate_shm_file_pair failed, file could be re-opened in read mode!");
        return false;
    }

    int ret;
    do {
        ret = ftruncate(rw_fd, size);
    } while (ret < 0 && errno == EINTR);
    if (ret < 0) {
        close(rw_fd);
        close(ro_fd);
        return false;
    }

    *p_rw_fd = rw_fd;
    *p_ro_fd = ro_fd;
    return true;
}

void keyboard_keymap_update(Keyboard* kb)
{
    const char* keymap_str = xkb_keymap_get_as_string(kb->xkb_keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
    if (!keymap_str) {
        log_error("Failed to get string version of keymap");
        return;
    }
    usz keymap_size = strlen(keymap_str) + 1;

    int rw_fd = -1;
    int ro_fd = -1;
    if (!allocate_shm_file_pair(keymap_size, &rw_fd, &ro_fd)) {
        log_error("Failed to allocate shm file for keymap");
        return;
    }

    void* dst = mmap(nullptr, keymap_size, PROT_READ | PROT_WRITE, MAP_SHARED, rw_fd, 0);
    close(rw_fd);
    if (dst == MAP_FAILED) {
        log_error("mmap failed");
        close(ro_fd);
        return;
    }

    memcpy(dst, keymap_str, keymap_size);
    munmap(dst, keymap_size);

    kb->keymap_fd = ro_fd;
    kb->keymap_size = keymap_size;

    log_debug("Successfully updated keyboard keymap fd: {}", kb->keymap_fd);

    for (wl_resource* resource : kb->wl_keyboard) {
        wl_keyboard_send_keymap(resource, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, kb->keymap_fd, kb->keymap_size);
    }
}

std::string escape_utf8(std::string_view in)
{
    std::string out;
    for (char c : in) {
        switch (c) {
            break;case '\r': out += "\\r";
            break;case '\n': out += "\\n";
            break;case '\b': out += "\\b";
            break;case '\t': out += "\\t";
            break;case '\f': out += "\\f";
            break;default:
                if (::isalpha(c) || ::isdigit(c)) {
                    out += c;
                } else {
                    out += std::format("\\{:x}", c);
                }
        }
    }
    return out;
}

void keyboard_key(Keyboard* kb, u32 libinput_keycode, bool pressed)
{
    u32 xkb_keycode = libinput_keycode + 8;
    char name[128] = {};
    char _utf[128] = {};

    if (!kb->focused && !kb->wl_keyboard.empty() && !kb->server->surfaces.empty()) {
        log_error("Sending keyboard enter!");
        kb->focused = kb->wl_keyboard.front();
        wl_keyboard_send_enter(kb->focused, wl_display_next_serial(kb->server->display),
            kb->server->surfaces.front()->wl_surface,
            ptr_to(to_array<u32>({})));
        wl_keyboard_send_modifiers(kb->focused, wl_display_get_serial(kb->server->display), 0, 0, 0, 0);
    }

    auto sym = xkb_state_key_get_one_sym(kb->xkb_state, xkb_keycode);
    xkb_keysym_get_name(sym, name, sizeof(name) - 1);

    xkb_state_key_get_utf8(kb->xkb_state, xkb_keycode, _utf, sizeof(_utf) - 1);
    auto utf = escape_utf8(_utf);

    if (strcmp(name, _utf) == 0) {
        log_debug("key '{}' ({}) = {}", utf, sym, pressed ? "press" : "release");
    } else if (!utf.empty()) {
        log_debug("key {} '{}' ({}) = {}", name, utf, sym, pressed ? "press" : "release");
    } else {
        log_debug("key {} ({}) = {}", name, sym, pressed ? "press" : "release");
    }

    if (kb->focused) {
        wl_keyboard_send_key(kb->focused,
            wl_display_next_serial(kb->server->display),
            server_get_elapsed_milliseconds(kb->server),
            libinput_keycode, pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
    }
}

void keyboard_modifiers(Keyboard* kb, u32 mods_depressed, u32 mods_latched, u32 mods_locked, u32 group)
{
    if (kb->focused) {
        wl_keyboard_send_modifiers(kb->focused,
            wl_display_next_serial(kb->server->display),
            mods_depressed,
            mods_latched,
            mods_locked,
            group);
    }
}