#pragma once

#include "pch.hpp"

using namespace std::literals;

using u64 = uint64_t;
using u32 = uint32_t;
using u16 = uint16_t;
using u8 = uint8_t;

using i64 = int64_t;
using i32 = int32_t;
using i16 = int16_t;
using i8 = int8_t;

using usz = size_t;
using isz = i64;

using c32 = char32_t;

using byte = std::byte;

using f32 = float;
using f64 = double;

// -----------------------------------------------------------------------------

using wrei_vec4f32 = glm:: vec4;
using wrei_vec2f64 = glm::dvec2;
using wrei_vec2i32 = glm::ivec2;

// -----------------------------------------------------------------------------

template<typename T>
struct wrei_rect
{
    glm::vec<2, T> origin;
    glm::vec<2, T> extent;

    bool contains(glm::vec<2, T> point)
    {
        return point.x >= origin.x && point.x <= origin.x + extent.x
            && point.y >= origin.y && point.y <= origin.y + extent.y;
    }
};
