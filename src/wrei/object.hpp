#pragma once

#include "types.hpp"
#include "log.hpp"
#include "util.hpp"

using wrei_object_version = u32;

struct wrei_object_meta
{
    size_t              size;
    u32                 ref_count;
    wrei_object_version version;
};

struct wrei_object
{
    wrei_object_meta wrei;

    wrei_object() = default;

    WREI_DELETE_COPY_MOVE(wrei_object)

    virtual ~wrei_object() = default;
};

struct wrei_registry
{
    struct allocated_block
    {
        void* data;
        wrei_object_version version;
    };

    std::array<std::vector<allocated_block>, 64> bins;
    u32 active_allocations;
    u32 inactive_allocations;
    u64 lifetime_allocations;

    ~wrei_registry();

    allocated_block allocate(usz size);
    void free(wrei_object*, wrei_object_version);
};

extern struct wrei_registry wrei_registry;

template<typename T>
T* wrei_create_unsafe()
{
    static constexpr auto size = wrei_round_up_power2(sizeof(T));
    wrei_registry::allocated_block block = wrei_registry.allocate(size);
    auto* t = new (block.data) T {};
    t->wrei.size = size;
    t->wrei.version = block.version;
    t->wrei.ref_count = 1;
    return t;
}

inline
void wrei_destroy(wrei_object* object, wrei_object_version version)
{
    wrei_registry.free(object, version);
}

// -----------------------------------------------------------------------------

template<typename T>
T* wrei_add_ref(T* t)
{
    if (t) static_cast<wrei_object*>(t)->wrei.ref_count++;
    return t;
}

template<typename T>
void wrei_remove_ref(T* t)
{
    if (t && !--static_cast<wrei_object*>(t)->wrei.ref_count) {
        wrei_destroy(t, t->wrei.version);
    }
}

// -----------------------------------------------------------------------------

struct wrei_ref_adopt_tag {};

template<typename T>
struct wrei_ref
{
    T* value;

    wrei_ref() = default;

    ~wrei_ref()
    {
        wrei_remove_ref(value);
    }

    wrei_ref(T* t)
        : value(t)
    {
        wrei_add_ref(t);
    }

    wrei_ref(T* t, wrei_ref_adopt_tag)
        : value(t)
    {}

    void reset(T* t = nullptr)
    {
        auto* v = value;
        if (t == v) return;
        wrei_remove_ref(v);
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

    operator bool() const { return value; }

    T* get() const { return value; }

    T* operator->() const { return value; }

    template<typename T2>
        requires std::derived_from<std::remove_cvref_t<T>, std::remove_cvref_t<T2>>
    operator wrei_ref<T2>() { return wrei_ref<T2>(value); }
};

template<typename T>
wrei_ref<T> wrei_adopt_ref(T* t)
{
    return {t, wrei_ref_adopt_tag{}};
}

template<typename T>
wrei_ref<T> wrei_create()
{
    return wrei_adopt_ref(wrei_create_unsafe<T>());
}

// -----------------------------------------------------------------------------

template<typename T>
struct wrei_weak
{
    wrei_object* value;
    wrei_object_version version;

    wrei_weak() = default;

    wrei_weak(T* t)
    {
        reset(t);
    }

    void reset(T* t = nullptr)
    {
        value = t;
        if (t) {
            version = t->wrei.version;
            assert(version != 0);
        }
    }

    wrei_weak& operator=(T* t)
    {
        reset(t);
        return *this;
    }

    constexpr bool operator==(const wrei_weak<T>& other) { return get() == other.get(); }
    constexpr bool operator!=(const wrei_weak<T>& other) { return get() != other.get(); }

    operator bool() const { return value && value->wrei.version == version;  }

    T* get() const { return *this ? static_cast<T*>(value) : nullptr; }

    T* operator->() const { return static_cast<T*>(value); }

    template<typename T2>
        requires std::derived_from<std::remove_cvref_t<T>, std::remove_cvref_t<T2>>
    operator wrei_weak<T2>() { return wrei_weak<T2>{value, version}; }
};

// -----------------------------------------------------------------------------

template<typename T>
using ref = wrei_ref<T>;

template<typename T>
using weak = wrei_weak<T>;
