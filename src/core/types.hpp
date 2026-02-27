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
using core_vec = glm::vec<L, T>;

using vec2u32 = core_vec<2, u32>;
using vec2i32 = core_vec<2, i32>;
using vec2f32 = core_vec<2, f32>;
using vec2f64 = core_vec<2, f64>;

using vec3f32 = core_vec<3, f32>;

using vec4f32 = core_vec<4, f32>;
using vec4u8  = core_vec<4,  u8>;

// -----------------------------------------------------------------------------

template<typename T>
struct core_aabb;

// -----------------------------------------------------------------------------

struct core_xywh_tag{};
static constexpr core_xywh_tag core_xywh;

struct core_minmax_tag{};
static constexpr core_minmax_tag core_minmax;

template<typename T>
struct core_rect
{
    core_vec<2, T> origin, extent;

    constexpr core_rect() = default;

    constexpr core_rect(core_vec<2, T> origin, core_vec<2, T> extent, core_xywh_tag)
        : origin(origin)
        , extent(extent)
    {}

    constexpr core_rect(core_vec<2, T> min, core_vec<2, T> max, core_minmax_tag)
        : origin(min)
        , extent(max - min)
    {}

    template<typename T2>
        requires (!std::same_as<T2, T>)
    constexpr core_rect(const core_rect<T2>& other)
        : core_rect(other.origin, other.extent, core_xywh)
    {}

    template<typename T2>
    constexpr core_rect(const core_aabb<T2>& other)
        : core_rect(other.min, other.max, core_minmax)
    {}

    constexpr bool operator==(const core_rect<T>& other) const = default;
};

using rect2i32 = core_rect<i32>;
using rect2f32 = core_rect<f32>;
using rect2f64 = core_rect<f64>;

// -----------------------------------------------------------------------------

template<typename T>
struct core_aabb
{
    core_vec<2, T> min, max;

    constexpr core_aabb() = default;

    constexpr core_aabb(core_vec<2, T> origin, core_vec<2, T> extent, core_xywh_tag)
        : min(origin)
        , max(origin + extent)
    {}

    constexpr core_aabb(core_vec<2, T> min, core_vec<2, T> max, core_minmax_tag)
        : min(min)
        , max(max)
    {}

    template<typename T2>
        requires (!std::same_as<T2, T>)
    constexpr core_aabb(const core_aabb<T2>& other)
        : core_aabb(other.min, other.max, core_minmax)
    {}

    template<typename T2>
    constexpr core_aabb(const core_rect<T2>& other)
        : core_aabb(other.origin, other.extent, core_xywh)
    {}

    constexpr bool operator==(const core_aabb<T>& other) const = default;
};

using aabb2i32 = core_aabb<i32>;
using aabb2f32 = core_aabb<f32>;
using aabb2f64 = core_aabb<f64>;
