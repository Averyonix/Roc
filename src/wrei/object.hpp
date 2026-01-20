#pragma once

#include "types.hpp"
#include "log.hpp"
#include "util.hpp"

// -----------------------------------------------------------------------------

/**
 * Common polymorphic helper base for dynamic objects.
 *
 * Note that no registry operations actually depend on deriving from this type.
 * It is merely provided as a convenience.
 */
struct wrei_object
{
    wrei_object() = default;

    WREI_DELETE_COPY_MOVE(wrei_object)

    virtual ~wrei_object() = default;
};

// -----------------------------------------------------------------------------

using wrei_allocation_version = u64;

struct alignas(16) wrei_allocation_header
{
    wrei_allocation_version version;
    u32 ref_count;
    u8 bin;
};

template<typename T>
wrei_allocation_header* wrei_allocation_from(T* t)
{
    // NOTE: Assumes that `t` points to start of allocated data section
    //       This will break for objects with virtual hierarchies, but we don't support them anyway..
    return reinterpret_cast<wrei_allocation_header*>(t) - 1;
}

inline
void* wrei_allocation_get_data(wrei_allocation_header* header)
{
    return header + 1;
}

// -----------------------------------------------------------------------------

struct wrei_registry
{
    std::array<std::vector<wrei_allocation_header*>, 64> bins;
    u32 active_allocations;
    u32 inactive_allocations;

    ~wrei_registry();

    wrei_allocation_header* allocate(usz size);
    void free(wrei_allocation_header*);
};

extern struct wrei_registry wrei_registry;

// -----------------------------------------------------------------------------

template<typename T>
void wrei_destruct(T* t)
{
    if constexpr (!std::is_trivially_destructible_v<T>) {
        t->~T();
    }
}

#define WREI_OBJECT_EXPLICIT_DECLARE(Type) \
    template<> void wrei_destruct(Type* t)

#define WREI_OBJECT_EXPLICIT_DEFINE(Type) \
    template<> void wrei_destruct(Type* t) \
    { \
        t->~Type(); \
    }

// -----------------------------------------------------------------------------

template<typename T>
T* wrei_create_uninitialized()
{
    return static_cast<T*>(wrei_allocation_get_data(wrei_registry.allocate(sizeof(T))));
}

template<typename T>
T* wrei_create_unsafe(auto&&... args)
{
    return new (wrei_create_uninitialized<T>()) T(std::forward<decltype(args)>(args)...);
}

template<typename T>
void wrei_destroy(T* t)
{
    wrei_destruct(t);
    wrei_registry.free(wrei_allocation_from(t));
}

// -----------------------------------------------------------------------------

template<typename T>
T* wrei_add_ref(T* t)
{
    if (t) wrei_allocation_from(t)->ref_count++;
    return t;
}

template<typename T>
T* wrei_remove_ref(T* t)
{
    if (!t) return nullptr;
    if (!--wrei_allocation_from(t)->ref_count) {
        wrei_destroy(t);
        return nullptr;
    }
    return t;
}

// -----------------------------------------------------------------------------

struct wrei_ref_adopt_tag {};

template<typename T>
struct wrei_ref
{
    T* value;

    // Destruction

    ~wrei_ref()
    {
        wrei_remove_ref(value);
    }

    // Construction + Assignment

    wrei_ref() = default;

    wrei_ref(T* t)
        : value(t)
    {
        wrei_add_ref(value);
    }

    wrei_ref(T* t, wrei_ref_adopt_tag)
        : value(t)
    {}

    void reset(T* t = nullptr)
    {
        if (t == value) return;
        wrei_remove_ref(value);
        value = wrei_add_ref(t);
    }

    wrei_ref& operator=(T* t)
    {
        reset(t);
        return *this;
    }

    wrei_ref(const wrei_ref& other)
        : value(wrei_add_ref(other.value))
    {}

    wrei_ref& operator=(const wrei_ref& other)
    {
        if (value != other.value) {
            wrei_remove_ref(value);
            value = wrei_add_ref(other.value);
        }
        return *this;
    }

    wrei_ref(wrei_ref&& other)
        : value(std::exchange(other.value, nullptr))
    {}

    wrei_ref& operator=(wrei_ref&& other)
    {
        if (value != other.value) {
            wrei_remove_ref(value);
            value = std::exchange(other.value, nullptr);
        }
        return *this;
    }

    // Queries

    explicit operator bool() const { return value; }
    T*        get() const { return value; }
    T* operator->() const { return value; }

    // Conversions

    template<typename T2>
    operator wrei_ref<T2>() { return wrei_ref<T2>(value); }
};

template<typename T>
wrei_ref<T> wrei_adopt_ref(T* t)
{
    return {t, wrei_ref_adopt_tag{}};
}

template<typename T>
wrei_ref<T> wrei_create(auto&&... args)
{
    return wrei_adopt_ref(wrei_create_unsafe<T>(std::forward<decltype(args)>(args)...));
}

// -----------------------------------------------------------------------------

template<typename T>
struct wrei_weak
{
    T* value;
    wrei_allocation_version version;

    // Construction + Assignment

    wrei_weak() = default;

    wrei_weak(T* t)
    {
        reset(t);
    }

    void reset(T* t = nullptr)
    {
        value = t;
        if (value) {
            version = wrei_allocation_from(value)->version;
        }
    }

    wrei_weak& operator=(T* t)
    {
        reset(t);
        return *this;
    }

    // Queries

    constexpr bool operator==(const wrei_weak<T>& other) { return get() == other.get(); }
    constexpr bool operator!=(const wrei_weak<T>& other) { return get() != other.get(); }

    explicit operator bool() const { return value && wrei_allocation_from(value)->version == version; }
    T*        get() const { return *this ? value : nullptr; }
    T* operator->() const { return value; }

    // Conversions

    template<typename T2>
    operator wrei_weak<T2>() { return wrei_weak<T2>{get()}; }
};

template<typename T>
bool weak_container_contains(const auto& haystack, T* needle)
{
    return std::ranges::contains(haystack, needle, &wrei_weak<T>::get);
}

// -----------------------------------------------------------------------------

template<typename T>
using ref = wrei_ref<T>;

template<typename T>
using weak = wrei_weak<T>;
