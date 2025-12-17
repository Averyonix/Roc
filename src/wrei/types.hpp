#pragma once

#include "pch.hpp"

static_assert(std::endian::native == std::endian::little);

// -----------------------------------------------------------------------------

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

template<glm::length_t L, typename T>
using wrei_vec = glm::vec<L, T>;

using vec2u32 = wrei_vec<2, u32>;
using vec2i32 = wrei_vec<2, i32>;
using vec2f32 = wrei_vec<2, f32>;
using vec2f64 = wrei_vec<2, f64>;

using vec3f32 = wrei_vec<3, f32>;

using vec4f32 = wrei_vec<4, f32>;

// -----------------------------------------------------------------------------

template<typename T>
struct wrei_rect
{
    wrei_vec<2, T> origin, extent;
};

using rect2i32 = wrei_rect<i32>;
using rect2f32 = wrei_rect<f32>;
using rect2f64 = wrei_rect<f64>;
