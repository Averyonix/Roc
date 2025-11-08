#include "display.hpp"

void keyboard_added(Keyboard*)
{

};

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

void keyboard_key(Keyboard* kb, uint32_t libinput_keycode, bool pressed)
{
    u32 xkb_keycode = libinput_keycode + 8;
    char name[128] = {};
    char _utf[128] = {};

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
}
