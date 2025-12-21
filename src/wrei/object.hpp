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

template<typename T>
void wrei_destroy(T* object, wrei_object_version version)
{
    wrei_registry.free(wrei_object_to_base(object), version);
}

// -----------------------------------------------------------------------------

template<typename T>
T* wrei_object_from_base(wrei_object* base)
{
    return static_cast<T*>(base);
}

template<typename T>
wrei_object* wrei_object_to_base(T* object)
{
    return static_cast<wrei_object*>(object);
}

#define WREI_OBJECT_EXPLICIT_DECLARE(Type) \
    template<> Type* wrei_object_from_base(wrei_object* base); \
    template<> wrei_object* wrei_object_to_base(Type* base)

#define WREI_OBJECT_EXPLICIT_DEFINE(Type) \
    template<> Type* wrei_object_from_base(wrei_object* base) \
    { \
        return static_cast<Type*>(base); \
    } \
    template<> wrei_object* wrei_object_to_base(Type* object) \
    { \
        return static_cast<wrei_object*>(object); \
    }

// -----------------------------------------------------------------------------

template<typename T>
T* wrei_add_ref(T* t)
{
    if (t) wrei_object_to_base(t)->wrei.ref_count++;
    return t;
}

template<typename T>
void wrei_remove_ref(T* t)
{
    if (!t) return;
    auto b = wrei_object_to_base(t);
    if (!--b->wrei.ref_count) {
        wrei_destroy(b, b->wrei.version);
    }
}

// -----------------------------------------------------------------------------

struct wrei_ref_adopt_tag {};

template<typename T>
struct wrei_ref
{
    wrei_object* value;

    wrei_ref() = default;

    ~wrei_ref()
    {
        wrei_remove_ref(value);
    }

    wrei_ref(T* t)
        : value(wrei_object_to_base(t))
    {
        wrei_add_ref(value);
    }

    wrei_ref(T* t, wrei_ref_adopt_tag)
        : value(wrei_object_to_base(t))
    {}

    void reset(T* t = nullptr)
    {
        auto* b = wrei_object_to_base(t);
        if (b == value) return;
        wrei_remove_ref(value);
        value = wrei_add_ref(b);
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

    T* get() const { return wrei_object_from_base<T>(value); }

    T* operator->() const { return get(); }

    template<typename T2>
        requires std::derived_from<std::remove_cvref_t<T>, std::remove_cvref_t<T2>>
    operator wrei_ref<T2>() { return wrei_ref<T2>(get()); }
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
        value = wrei_object_to_base(t);
        if (value) {
            version = value->wrei.version;
            assert(version != 0);
        }
    }

    wrei_weak& operator=(T* t)
    {
        reset(t);
        return *this;
    }

private:
    wrei_object* base() const { return *this ? value : nullptr; }

public:
    constexpr bool operator==(const wrei_weak<T>& other) { return base() == other.base(); }
    constexpr bool operator!=(const wrei_weak<T>& other) { return base() != other.base(); }

    operator bool() const { return value && value->wrei.version == version;  }

    T* get() const { return *this ? wrei_object_from_base<T>(value) : nullptr; }

    T* operator->() const { return wrei_object_from_base<T>(value); }

    template<typename T2>
        requires std::derived_from<std::remove_cvref_t<T>, std::remove_cvref_t<T2>>
    operator wrei_weak<T2>() { return wrei_weak<T2>{value, version}; }
};

// -----------------------------------------------------------------------------

template<typename T>
using ref = wrei_ref<T>;

template<typename T>
using weak = wrei_weak<T>;
