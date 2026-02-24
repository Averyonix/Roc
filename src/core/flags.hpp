#pragma once

#include "util.hpp"

template<typename E>
    requires std::is_enum_v<E>
struct core_flags
{
    using underlying_type = std::underlying_type_t<E>;

    underlying_type value;

    constexpr core_flags() = default;
    constexpr core_flags(E e): value(std::to_underlying(e)) {}
    explicit constexpr core_flags(E e, auto... rest) : value((e || ... || std::to_underlying(rest))) {}

#define BINARY_OP(Name, Op, ...) \
    friend constexpr core_flags operator Name(core_flags a, core_flags b) { return {E(a.value Op __VA_ARGS__ b.value)}; } \
    constexpr core_flags& operator Name##=(core_flags other) { value Op##= __VA_ARGS__ other.value; return *this; }

    BINARY_OP(|, |)
    BINARY_OP(&, &)
    BINARY_OP(-, &, ~)

#undef BINARY_OP

    constexpr E get() const noexcept { return E(value); }
    constexpr bool contains(core_flags set) const noexcept { return (value & set.value) == set.value; }
    constexpr bool empty() const noexcept { return !value; }
    explicit operator bool() const noexcept { return !empty(); }

    friend constexpr bool operator==(core_flags a, core_flags b) = default;
};

template<typename E>
struct std::hash<core_flags<E>>
{
    usz operator()(const core_flags<E>& v)
    {
        return std::hash<typename core_flags<E>::underlying_type>{}(v.value);
    }
};

template<typename E>
    requires std::is_scoped_enum_v<E>
constexpr core_flags<E> operator|(E a, E b)
{
    return core_flags(a) | b;
}

template<typename Enum>
std::string core_to_string(core_flags<Enum> bitfield)
{
    std::string result;

    using Type = core_flags<Enum>::underlying_type;
    Type v = bitfield.value;

    while (v) {
        Type lsb = Type(1) << std::countr_zero(v);
        if (!result.empty()) result += "|";
        result += core_enum_to_string(Enum(lsb));
        v &= ~lsb;
    }

    return result;
}

template<typename E>
using flags = core_flags<E>;
