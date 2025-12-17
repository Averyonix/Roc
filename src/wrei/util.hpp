#pragma once

#include "pch.hpp"
#include "log.hpp"
#include "types.hpp"

// -----------------------------------------------------------------------------

template<typename Fn>
struct wrei_defer_guard
{
    Fn fn;

    wrei_defer_guard(Fn&& fn): fn(std::move(fn)) {}
    ~wrei_defer_guard() { fn(); };
};

#define defer wrei_defer_guard _ = [&]

// -----------------------------------------------------------------------------

template<typename... Ts>
struct wrei_overload_set : Ts... {
    using Ts::operator()...;
};

template<typename... Ts> wrei_overload_set(Ts...) -> wrei_overload_set<Ts...>;

// -----------------------------------------------------------------------------

constexpr
std::string wrei_ascii_to_upper(std::string_view in)
{
    std::string out(in);
    for (char& c : out) c = std::toupper(c);
    return out;
}

// -----------------------------------------------------------------------------

constexpr auto wrei_ptr_to(auto&& value) { return &value; }

// -----------------------------------------------------------------------------

#define WREI_FUNC_REF(func) [](void* d, auto... args) { return (*static_cast<decltype(func)*>(d))(std::forward<decltype(args)>(args)...); }, &func

// -----------------------------------------------------------------------------

#define WREI_DECORATE_FLAG_ENUM(EnumType) \
    inline constexpr EnumType operator| (EnumType  l, EnumType r) { return EnumType(std::to_underlying(l) | std::to_underlying(r));                  } \
    inline constexpr EnumType operator|=(EnumType& l, EnumType r) { return l = l | r;                                                                } \
    inline constexpr bool     operator>=(EnumType  l, EnumType r) { return std::to_underlying(r) == (std::to_underlying(l) & std::to_underlying(r)); } \
    inline constexpr bool     operator< (EnumType  l, EnumType r) { return !(l >= r);                                                                } \
    inline constexpr EnumType operator& (EnumType  l, EnumType r) { return EnumType(std::to_underlying(l) & std::to_underlying(r));                  } \
    inline constexpr EnumType operator~ (EnumType  v)             { return EnumType(~std::to_underlying(v));                                         } \
    inline constexpr EnumType operator-=(EnumType& l, EnumType r) { return l = EnumType(std::to_underlying(l) & ~std::to_underlying(r));             }

// -----------------------------------------------------------------------------

#define WREI_DELETE_COPY_MOVE(Type)         \
               Type(const Type& ) = delete; \
    Type& operator=(const Type& ) = delete; \
               Type(      Type&&) = delete; \
    Type& operator=(      Type&&) = delete;

// -----------------------------------------------------------------------------

template<typename T, typename E>
struct wrei_enum_map
{
    T _data[magic_enum::enum_count<E>()];

    static constexpr auto enum_values = magic_enum::enum_values<E>();

    constexpr       T& operator[](E value)       { return _data[magic_enum::enum_index(value).value()]; }
    constexpr const T& operator[](E value) const { return _data[magic_enum::enum_index(value).value()]; }
};

// -----------------------------------------------------------------------------

constexpr vec2f64 wrei_copysign(     vec2f64 v, vec2f64 s) { return vec2f64(std::copysign(v.x, s.x), std::copysign(v.y, s.y)); }
constexpr vec2f64 wrei_round_to_zero(vec2f64 v)         { return wrei_copysign(glm::floor(glm::abs(v)), v);                   }

// -----------------------------------------------------------------------------

template<typename T>
auto wrei_iterate(std::span<T> view, bool reverse = false)
{
    struct iterator
    {
        std::span<T> view;
        i64 cur, end, step;

        bool operator==(std::default_sentinel_t) const { return cur == end; }
        void operator++() { cur += step; }
        T&   operator*()  { return view[cur]; }
    };

    struct iterable
    {
        std::span<T> view;
        bool backward;

        iterator begin() {
            return backward
                ? iterator { view, i64(view.size()) - 1, -1, -1 }
                : iterator { view, 0, i64(view.size()), 1 };
        }

        std::default_sentinel_t end() { return {}; }
    };

    return iterable{view, reverse};
}

// -----------------------------------------------------------------------------

struct wrei_command_parser
{
    std::span<const std::string_view> args;
    u32 index;

    operator bool() const { return index < args.size(); }

    bool match(std::string_view arg)
    {
        if (index < args.size() && args[index] == arg) {
            index++;
            return true;
        }
        return false;
    }

    std::span<const std::string_view> peek_rest() { return args.subspan(index); }

    std::string_view peek()       { return index < args.size() ? args[index]   : std::string_view{}; }
    std::string_view get_string() { return index < args.size() ? args[index++] : std::string_view{}; }

    template<typename T>
    std::optional<T> get_from_chars()
    {
        if (index >= args.size()) return std::nullopt;

        T value;
        auto res = std::from_chars(args[index].begin(), args[index].end(), value);
        if (!res) return std::nullopt;

        index++;

        return value;
    }

    std::optional<int> get_int()    { return get_from_chars<int>(); }
    std::optional<int> get_double() { return get_from_chars<f64>(); }
};

// -----------------------------------------------------------------------------

std::string wrei_duration_to_string(std::chrono::duration<f64, std::nano> dur);

// -----------------------------------------------------------------------------

inline
std::string wrei_escape_utf8(std::string_view in)
{
    std::string out;
    for (char c : in) {
        switch (c) {
            break;case '\r': out += "\\r";
            break;case '\n': out += "\\n";
            break;case '\b': out += "\\b";
            break;case '\t': out += "\\t";
            break;case '\f': out += "\\f";
            break;default:
                if (::isalpha(c) || ::isdigit(c)) {
                    out += c;
                } else {
                    out += std::format("\\{:x}", c);
                }
        }
    }
    return out;
}

// -----------------------------------------------------------------------------

inline
void wrei_log_unix_error(std::string_view message, int err = 0)
{
    err = err ?: errno;

    if (message.empty()) { log_error("({}) {}",              err, strerror(err)); }
    else                 { log_error("{}: ({}) {}", message, err, strerror(err)); }
}

enum class wrei_unix_error_behavior : u32
{
    ret_null,
    ret_neg1,
    ret_neg_errno,
    check_errno,
};

template<wrei_unix_error_behavior B>
struct wrei_unix_check_helper
{
    template<typename T>
    static constexpr
    T check(std::source_location loc, T res, auto... allowed)
    {
        bool error_occured = false;
        int error_code = 0;

        if constexpr (B == wrei_unix_error_behavior::ret_null)     if (!res)      { error_occured = true; error_code = errno; }
        if constexpr (B == wrei_unix_error_behavior::ret_neg1)     if (res == -1) { error_occured = true; error_code = errno; }
        if constexpr (B == wrei_unix_error_behavior::ret_neg_errno) if (res < 0)   { error_occured = true; error_code = -res;  }
        if constexpr (B == wrei_unix_error_behavior::check_errno)  if (errno)     { error_occured = true; error_code = errno; }

        if (!error_occured || (... || (error_code == allowed))) return res;

        wrei_log_unix_error(std::format("unix_check {}@{}", loc.file_name(), loc.line()), error_code);

        return res;
    }
};

#define wrei_unix_check_null(func, ...)                       wrei_unix_check_helper<wrei_unix_error_behavior::ret_null     >::check(std::source_location::current(), (func) __VA_OPT__(,) __VA_ARGS__)
#define wrei_unix_check_n1(func, ...)                         wrei_unix_check_helper<wrei_unix_error_behavior::ret_neg1     >::check(std::source_location::current(), (func) __VA_OPT__(,) __VA_ARGS__)
#define wrei_unix_check_ne(func, ...)                         wrei_unix_check_helper<wrei_unix_error_behavior::ret_neg_errno>::check(std::source_location::current(), (func) __VA_OPT__(,) __VA_ARGS__)
#define wrei_unix_check_ce(func, ...) [&] { errno = 0; return wrei_unix_check_helper<wrei_unix_error_behavior::check_errno  >::check(std::source_location::current(), (func) __VA_OPT__(,) __VA_ARGS__); }()

// -----------------------------------------------------------------------------

template<typename T>
bool wrei_rect_contains(const wrei_rect<T>& rect, wrei_vec<2, T> point)
{
    return point.x >= rect.origin.x && point.x <= rect.origin.x + rect.extent.x
        && point.y >= rect.origin.y && point.y <= rect.origin.y + rect.extent.y;
}

// -----------------------------------------------------------------------------

struct wrei_region
{
    pixman_region32 region;

    wrei_region();
    wrei_region(rect2i32);

    wrei_region(const wrei_region&);
    wrei_region& operator=(const wrei_region&);

    wrei_region(wrei_region&&);
    wrei_region& operator=(wrei_region&&);

    ~wrei_region();

    void clear();

    bool empty() const;

    void add(rect2i32);
    void subtract(rect2i32);

    bool contains(vec2i32 point) const;
    bool contains(rect2i32 rect) const;
};

// -----------------------------------------------------------------------------

constexpr usz wrei_round_up_power2(usz v) noexcept
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

// -----------------------------------------------------------------------------

inline
void wrei_debugbreak()
{
    raise(SIGTRAP);
}
