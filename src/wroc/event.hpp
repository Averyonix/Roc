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
    pointer_absolute, // Pointer absolute and relative should be pre-processed into a single "motion" type
    pointer_relative,
    pointer_axis,
};

struct wroc_event
{
    wroc_event_type type;
};

struct wroc_output_event : wroc_event
{
    wroc_output* output;
};

struct wroc_keyboard_event : wroc_event
{
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

struct wroc_pointer_event : wroc_event
{
    wroc_pointer* pointer;
    wroc_output* output;

    union {
        struct {
            u32 button;
            bool pressed;
        } button;
        struct {
            wrei_vec2f64 position;
        } absolute;
        struct {
            wrei_vec2f64 delta;
        } relative;
        struct {
            wrei_vec2f64 delta;
        } axis;
    };
};

void wroc_post_event(wroc_server*, const wroc_event& event);

bool wroc_handle_movesize_interaction(wroc_server*, const wroc_event& event);

void wroc_handle_output_event(  wroc_server*, const wroc_output_event&);
void wroc_handle_keyboard_event(wroc_server*, const wroc_keyboard_event&);
void wroc_handle_pointer_event( wroc_server*, const wroc_pointer_event&);
