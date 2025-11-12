#include "server.hpp"

void wroc_pointer_added(wroc_pointer* pointer)
{
    (void)pointer;
}

void wroc_pointer_button(wroc_pointer* pointer, u32 button, bool pressed)
{
    (void)pointer;
    (void)button;
    (void)pressed;
}

void wroc_pointer_absolute(wroc_pointer* pointer, wroc_output*, wrei_vec2f64 pos)
{
    (void)pointer;
    (void)pos;
}

void wroc_pointer_relative(wroc_pointer* pointer, wrei_vec2f64 rel)
{
    (void)pointer;
    (void)rel;
}

void wroc_pointer_axis(wroc_pointer* pointer, wrei_vec2f64 rel)
{
    (void)pointer;
    (void)rel;
}
