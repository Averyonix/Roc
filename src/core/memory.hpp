#pragma once

#include "types.hpp"

struct core_byte_view
{
    const void* data;
    usz         size;
};

inline
auto core_view_bytes(auto&& object) -> core_byte_view
{
    return { &object, sizeof(object) };
}

// -----------------------------------------------------------------------------

struct core_fmt_bytes
{
    u64 bytes;

    core_fmt_bytes() = default;
    core_fmt_bytes(u64 size): bytes(size) {}
};

std::string core_to_string(core_fmt_bytes size);

// -----------------------------------------------------------------------------

template<typename T>
T* core_byte_offset_pointer(void* source, isz offset)
{
    return reinterpret_cast<T*>(reinterpret_cast<byte*>(source) + offset);
}

// -----------------------------------------------------------------------------

inline
usz core_compute_geometric_growth(usz current_size, usz new_min_size)
{
    usz geometric = current_size + (current_size >> 1);
    return std::max(geometric, new_min_size);
}
