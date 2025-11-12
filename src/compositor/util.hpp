#pragma once

#include "common/types.hpp"

#define WROC_STUB [](auto...) {}

// -----------------------------------------------------------------------------

template<typename T>
std::span<T> wroc_to_span(wl_array* array)
{
    usz count = array->size / sizeof(T);
    return std::span<T>(static_cast<T*>(array->data), count);
}

template<typename T>
wl_array wroc_to_wl_array(std::span<T> span)
{
    return wl_array {
        .size = span.size_bytes(),
        .alloc = span.size_bytes(),
        .data = const_cast<void*>(static_cast<const void*>(span.data())),
    };
}
