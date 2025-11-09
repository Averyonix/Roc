#include "display.hpp"

void pointer_added(Pointer* pointer)
{
    (void)pointer;
}

void pointer_button(Pointer* pointer, u32 button, bool pressed)
{
    (void)pointer;
    (void)button;
    (void)pressed;
}

void pointer_absolute(Pointer* pointer, Output*, vec2 pos)
{
    (void)pointer;
    (void)pos;
}

void pointer_relative(Pointer* pointer, vec2 rel)
{
    (void)pointer;
    (void)rel;
}

void pointer_axis(Pointer* pointer, vec2 rel)
{
    (void)pointer;
    (void)rel;
}
