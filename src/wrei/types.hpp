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
struct wrei_aabb;

// -----------------------------------------------------------------------------

struct wrei_xywh_tag{};
static constexpr wrei_xywh_tag wrei_xywh;

struct wrei_minmax_tag{};
static constexpr wrei_minmax_tag wrei_minmax;

template<typename T>
struct wrei_rect
{
    wrei_vec<2, T> origin, extent;

    wrei_rect() = default;

    wrei_rect(wrei_vec<2, T> origin, wrei_vec<2, T> extent, wrei_xywh_tag)
        : origin(origin)
        , extent(extent)
    {}

    wrei_rect(wrei_vec<2, T> min, wrei_vec<2, T> max, wrei_minmax_tag)
        : origin(min)
        , extent(max - min)
    {}

    template<typename T2>
        requires (!std::same_as<T2, T>)
    wrei_rect(const wrei_rect<T2>& other)
        : origin(other.origin)
        , extent(other.extent)
    {}

    template<typename T2>
    wrei_rect(const wrei_aabb<T2>& other)
        : origin(other.min)
        , extent(other.max - other.min)
    {}

    constexpr bool operator==(const wrei_rect<T>& other) const = default;
};

using rect2i32 = wrei_rect<i32>;
using rect2f32 = wrei_rect<f32>;
using rect2f64 = wrei_rect<f64>;

// -----------------------------------------------------------------------------

template<typename T>
struct wrei_aabb
{
    wrei_vec<2, T> min, max;

    wrei_aabb() = default;

    wrei_aabb(wrei_vec<2, T> origin, wrei_vec<2, T> extent, wrei_xywh_tag)
        : min(origin)
        , max(origin + extent)
    {}

    wrei_aabb(wrei_vec<2, T> min, wrei_vec<2, T> max, wrei_minmax_tag)
        : min(min)
        , max(max)
    {}

    template<typename T2>
        requires (!std::same_as<T2, T>)
    wrei_aabb(const wrei_aabb<T2>& other)
        : min(other.min)
        , max(other.max)
    {}

    template<typename T2>
    wrei_aabb(const wrei_rect<T2>& other)
        : min(other.origin)
        , max(other.origin + other.extent)
    {}

    constexpr bool operator==(const wrei_aabb<T>& other) const = default;
};

using aabb2i32 = wrei_aabb<i32>;
using aabb2f32 = wrei_aabb<f32>;
using aabb2f64 = wrei_aabb<f64>;
