#pragma once

#include "types.hpp"

inline
auto as_bytes(const void* data, usz size) -> std::span<const byte>
{
    return { reinterpret_cast<const byte*>(data), size };
}

inline
auto view_bytes(auto&& object) -> std::span<const byte>
{
    return { reinterpret_cast<const byte*>(&object), sizeof(object) };
}

// -----------------------------------------------------------------------------

struct FmtBytes
{
    u64 bytes;

    FmtBytes() = default;
    FmtBytes(u64 size): bytes(size) {}
};

std::string to_string(FmtBytes size);

// -----------------------------------------------------------------------------

template<typename T>
T* byte_offset_pointer(void* source, isz offset)
{
    return reinterpret_cast<T*>(reinterpret_cast<byte*>(source) + offset);
}

// -----------------------------------------------------------------------------

inline
usz compute_geometric_growth(usz current_size, usz new_min_size)
{
    usz geometric = current_size + (current_size >> 1);
    return std::max(geometric, new_min_size);
}

// -----------------------------------------------------------------------------

template<typename T>
constexpr
T align_up_power2(T v, u64 align) noexcept
{
    return T((u64(v) + (align - 1)) &~ (align - 1));
}
