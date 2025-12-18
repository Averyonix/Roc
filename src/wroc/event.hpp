#include "server.hpp"

enum class wroc_event_type
{
    output_added,
    output_removed,
    output_frame,

    keyboard_added,
    keyboard_keymap,
    keyboard_key,
    keyboard_modifiers,

    pointer_added,
    pointer_button,
    pointer_motion,
    pointer_axis,
};

struct wroc_event {};

#define WROC_EVENT_BASE wroc_event_type type;

struct wroc_event_base : wroc_event
{
    WROC_EVENT_BASE
};

inline
wroc_event_type wroc_event_get_type(const wroc_event& event)
{
    return static_cast<const wroc_event_base&>(event).type;
}

struct wroc_output_event : wroc_event
{
    WROC_EVENT_BASE

    wroc_output* output;
};

struct wroc_keyboard_event : wroc_event
{
    WROC_EVENT_BASE

    wroc_keyboard* keyboard;

    union {
        struct {
            u32 keycode;
            bool pressed;
        } key;
        struct {
            u32 depressed;
            u32 latched;
            u32 locked;
            u32 group;
        } mods;
    };
};

inline
u32 wroc_key_to_xkb(u32 libinput_code)
{
    return libinput_code + 8;
}

struct wroc_pointer_event : wroc_event
{
    WROC_EVENT_BASE

    wroc_pointer* pointer;
    wroc_output* output;

    union {
        struct {
            u32 button;
            bool pressed;
        } button;
        struct {
            vec2f64 delta;
        } motion;
        struct {
            vec2f64 delta;
        } axis;
    };
};

void wroc_post_event(wroc_server*, const wroc_event& event);

bool wroc_handle_movesize_interaction(wroc_server*, const wroc_event& event);

void wroc_handle_output_event(  wroc_server*, const wroc_output_event&);
void wroc_handle_keyboard_event(wroc_server*, const wroc_keyboard_event&);
void wroc_handle_pointer_event( wroc_server*, const wroc_pointer_event&);
