#include "wroc.hpp"

enum class wroc_event_type : u32
{
    output_added,
    output_removed,

    // Sent when an output is ready to accept a new frame.
    // This does not necessarily correlate with any vblank periods.
    output_frame,

    // Sent when a commit is flipped or composited.
    // This value should be close to the last point at which the point could have been submitted and made ready.
    // For synchronized page flips, this should correspond to an output_frame event, but may be sent at any time
    // when asynchronous (tearing) page flips are enabled.
    //
    // This value is also used for client presentation timing, treated as scanout time. This is valid for the direct
    // backend, but treats the composition step of the parent compositor as scanout (even if the actual contents is,
    // as is often the case, delayed by an extra frame internally). This inaccuracy in layered mode is acceptable.
    output_commit,

    keyboard_key,
    keyboard_modifiers,

    pointer_button,
    pointer_motion,
    pointer_axis,
};

struct wroc_event {};

#define WROC_EVENT_BASE \
    wroc_event_type type; \
    std::chrono::steady_clock::time_point timestamp; \

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

    union {
        struct {
            wroc_output_commit_id id;
            std::chrono::steady_clock::time_point start;
        } commit;
    };
};

using evdev_key_t = u32;

struct wroc_keyboard_event : wroc_event
{
    WROC_EVENT_BASE

    wroc_seat_keyboard* keyboard;

    union {
        struct {
            evdev_key_t code;
            xkb_keysym_t symbol;
            const char* utf8;
            bool pressed;

            [[nodiscard]] constexpr xkb_keysym_t upper() const noexcept { return xkb_keysym_to_upper(symbol); };
            [[nodiscard]] constexpr xkb_keysym_t lower() const noexcept { return xkb_keysym_to_lower(symbol); };
        } key;
        struct {
            xkb_mod_mask_t depressed;
            xkb_mod_mask_t latched;
            xkb_mod_mask_t locked;
            xkb_mod_mask_t group;
        } mods;
    };
};

inline
xkb_keycode_t wroc_key_to_xkb(evdev_key_t code)
{
    return code + 8;
}

struct wroc_pointer_event : wroc_event
{
    WROC_EVENT_BASE

    wroc_seat_pointer* pointer;

    union {
        struct {
            evdev_key_t button;
            bool pressed;
        } button;
        struct {
            vec2f64 rel;
            vec2f64 rel_unaccel;
        } motion;
        struct {
            vec2f64 delta;
        } axis;
    };
};

void wroc_post_event(const wroc_event&);

bool wroc_handle_zone_interaction(const wroc_event&);
bool wroc_handle_focus_cycle_interaction(const wroc_event&);
bool wroc_handle_movesize_interaction(const wroc_event&);

void wroc_handle_output_event(  const wroc_output_event&);
void wroc_handle_keyboard_event(const wroc_keyboard_event&);
void wroc_handle_pointer_event( const wroc_pointer_event&);
