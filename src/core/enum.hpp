#pragma once

#include "types.hpp"

template<typename Enum>
    requires std::is_enum_v<Enum>
std::string_view to_string(Enum value)
{
    std::string_view name = magic_enum::enum_name(value);
    return name;
}

// -----------------------------------------------------------------------------

template<typename E>
    requires std::is_enum_v<E>
struct Flags
{
    using underlying_type = std::underlying_type_t<E>;

    underlying_type value;

    constexpr Flags() = default;
    constexpr Flags(E e): value(std::to_underlying(e)) {}
    explicit constexpr Flags(E e, auto... rest) : value((e || ... || std::to_underlying(rest))) {}

#define BINARY_OP(Name, Op, ...) \
    friend constexpr Flags operator Name(Flags a, Flags b) { return {E(a.value Op __VA_ARGS__ b.value)}; } \
    constexpr Flags& operator Name##=(Flags other) { value Op##= __VA_ARGS__ other.value; return *this; }

    BINARY_OP(|, |)
    BINARY_OP(&, &)
    BINARY_OP(-, &, ~)

#undef BINARY_OP

    constexpr E get() const noexcept { return E(value); }
    constexpr bool contains(Flags set) const noexcept { return (value & set.value) == set.value; }
    constexpr bool empty() const noexcept { return !value; }
    explicit operator bool() const noexcept { return !empty(); }

    friend constexpr bool operator==(Flags a, Flags b) = default;
};

template<typename E>
struct std::hash<Flags<E>>
{
    usz operator()(const Flags<E>& v)
    {
        return std::hash<typename Flags<E>::underlying_type>{}(v.value);
    }
};

template<typename E>
    requires std::is_scoped_enum_v<E>
constexpr Flags<E> operator|(E a, E b)
{
    return Flags(a) | b;
}

template<typename Enum>
    requires std::is_enum_v<Enum>
std::string to_string(Flags<Enum> bitfield)
{
    std::string result;

    using Type = Flags<Enum>::underlying_type;
    Type v = bitfield.value;

    while (v) {
        Type lsb = Type(1) << std::countr_zero(v);
        if (!result.empty()) result += "|";
        result += to_string(Enum(lsb));
        v &= ~lsb;
    }

    return result;
}
