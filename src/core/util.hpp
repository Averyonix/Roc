#pragma once

#include "pch.hpp"
#include "log.hpp"
#include "types.hpp"

// -----------------------------------------------------------------------------

template<typename Fn>
struct DeferGuard
{
    Fn fn;

    DeferGuard(Fn&& fn): fn(std::move(fn)) {}
    ~DeferGuard() { fn(); };
};

#define defer DeferGuard _ = [&]

// -----------------------------------------------------------------------------

template<typename... Ts>
struct OverloadSet : Ts... {
    using Ts::operator()...;
};

template<typename... Ts> OverloadSet(Ts...) -> OverloadSet<Ts...>;

// -----------------------------------------------------------------------------

constexpr auto ptr_to(auto&& value) { return &value; }

// -----------------------------------------------------------------------------

#define DELETE_COPY(Type) \
               Type(const Type& ) = delete; \
    Type& operator=(const Type& ) = delete; \

#define DELETE_COPY_MOVE(Type) \
    DELETE_COPY(Type) \
               Type(Type&&) = delete; \
    Type& operator=(Type&&) = delete;

// -----------------------------------------------------------------------------

auto range_count(auto&& range)
{
    return std::ranges::count_if(range, [](const auto&) { return true; });
}

// -----------------------------------------------------------------------------

constexpr usz round_up_power2(usz v) noexcept
{
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    v++;

    return v;
}
