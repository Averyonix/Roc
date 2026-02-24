#pragma once

#include "util.hpp"

template<typename E>
    requires std::is_enum_v<E>
struct wrei_flags
{
    using underlying_type = std::underlying_type_t<E>;

    underlying_type value;

    constexpr wrei_flags() = default;
    constexpr wrei_flags(E e): value(std::to_underlying(e)) {}
    explicit constexpr wrei_flags(E e, auto... rest) : value((e || ... || std::to_underlying(rest))) {}

#define BINARY_OP(Name, Op, ...) \
    friend constexpr wrei_flags operator Name(wrei_flags a, wrei_flags b) { return {E(a.value Op __VA_ARGS__ b.value)}; } \
    constexpr wrei_flags& operator Name##=(wrei_flags other) { value Op##= __VA_ARGS__ other.value; return *this; }

    BINARY_OP(|, |)
    BINARY_OP(&, &)
    BINARY_OP(-, &, ~)

#undef BINARY_OP

    constexpr E get() const noexcept { return E(value); }
    constexpr bool contains(wrei_flags set) const noexcept { return (value & set.value) == set.value; }
    constexpr bool empty() const noexcept { return !value; }
    explicit operator bool() const noexcept { return !empty(); }

    friend constexpr bool operator==(wrei_flags a, wrei_flags b) = default;
};

template<typename E>
struct std::hash<wrei_flags<E>>
{
    usz operator()(const wrei_flags<E>& v)
    {
        return std::hash<typename wrei_flags<E>::underlying_type>{}(v.value);
    }
};

template<typename E>
    requires std::is_scoped_enum_v<E>
constexpr wrei_flags<E> operator|(E a, E b)
{
    return wrei_flags(a) | b;
}

template<typename Enum>
std::string wrei_to_string(wrei_flags<Enum> bitfield)
{
    std::string result;

    using Type = wrei_flags<Enum>::underlying_type;
    Type v = bitfield.value;

    while (v) {
        Type lsb = Type(1) << std::countr_zero(v);
        if (!result.empty()) result += "|";
        result += wrei_enum_to_string(Enum(lsb));
        v &= ~lsb;
    }

    return result;
}

template<typename E>
using flags = wrei_flags<E>;
