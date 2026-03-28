#pragma once

#include "types.hpp"
#include "log.hpp"
#include "util.hpp"
#include "debug.hpp"

// -----------------------------------------------------------------------------

using AllocationVersion = u32;

struct alignas(16) Allocation
{
    void(*free)(Allocation*);
    AllocationVersion version;
    u32 ref_count;
};

inline
auto allocation_from(const void* v) -> Allocation*
{
    // `const_cast` is safe as the `Allocation` is always mutable
    return static_cast<Allocation*>(const_cast<void*>(v)) - 1;
}

inline
void* allocation_get_data(Allocation* header)
{
    return header + 1;
}

// -----------------------------------------------------------------------------

struct RegistryStats
{
    u32 active_allocations;
    u32 inactive_allocations;
};

auto registry_get_stats() -> RegistryStats;

auto registry_allocate(u8 bin) -> Allocation*;
void registry_free(Allocation*, u8 bin);

constexpr
u8   registry_get_bin_index(usz size)
{
    return std::countr_zero(round_up_power2(size + sizeof(Allocation)));
}

// -----------------------------------------------------------------------------

template<typename T>
T* object_create_uninitialized()
{
    static constexpr auto bin = registry_get_bin_index(sizeof(T));
    auto header = registry_allocate(bin);
    header->free = [](Allocation* header) {
        if constexpr (!std::is_trivially_destructible_v<T>) {
            static_cast<T*>(allocation_get_data(header))->~T();
        }
        registry_free(header, bin);
    };
    return static_cast<T*>(allocation_get_data(header));
}

template<typename T>
T* object_create_unsafe(auto&&... args)
{
    return new (object_create_uninitialized<T>()) T(std::forward<decltype(args)>(args)...);
}

inline
void object_destroy(void* v)
{
    auto header = allocation_from(v);
    header->free(header);
}

// -----------------------------------------------------------------------------

template<typename T>
T* object_add_ref(T* t)
{
    if (t) allocation_from(t)->ref_count++;
    return t;
}

template<typename T>
T* object_remove_ref(T* t)
{
    if (!t) return nullptr;
    auto header = allocation_from(t);
    if (!--header->ref_count) {
        header->free(header);
        return nullptr;
    }
    return t;
}

// -----------------------------------------------------------------------------

struct RefAdoptTag {};

template<typename T>
struct Ref
{
    T* value;

    // Destruction

    ~Ref()
    {
        object_remove_ref(value);
    }

    void destroy()
    {
        if (value) {
            debug_assert(allocation_from(value)->ref_count == 1);
            reset();
        }
    }

    // Construction + Assignment

    Ref() = default;

    Ref(T* t)
        : value(t)
    {
        object_add_ref(value);
    }

    Ref(T* t, RefAdoptTag)
        : value(t)
    {}

    void reset(T* t = nullptr)
    {
        if (t == value) return;
        object_remove_ref(value);
        value = object_add_ref(t);
    }

    Ref& operator=(T* t)
    {
        reset(t);
        return *this;
    }

    Ref(const Ref& other)
        : value(object_add_ref(other.value))
    {}

    Ref& operator=(const Ref& other)
    {
        if (value != other.value) {
            object_remove_ref(value);
            value = object_add_ref(other.value);
        }
        return *this;
    }

    Ref(Ref&& other)
        : value(std::exchange(other.value, nullptr))
    {}

    Ref& operator=(Ref&& other)
    {
        if (value != other.value) {
            object_remove_ref(value);
            value = std::exchange(other.value, nullptr);
        }
        return *this;
    }

    // Queries

    template<typename T2>
    bool operator==(const Ref<T2>& other) const { return value == other.value; };

    explicit operator bool() const { return value; }
    T*                 get() const { return value; }
    T*          operator->() const { return value; }

    // Conversions

    template<typename T2>
    operator Ref<T2>() { return Ref<T2>(value); }
};

template<typename T>
auto ref_adopt(T* t) -> Ref<T>
{
    return {t, RefAdoptTag{}};
}

template<typename T>
auto ref_create(auto&&... args) -> Ref<T>
{
    return ref_adopt(object_create_unsafe<T>(std::forward<decltype(args)>(args)...));
}

// -----------------------------------------------------------------------------

template<typename T>
struct Weak
{
    T* value;
    AllocationVersion version;

    // Construction + Assignment

    Weak() = default;

    Weak(T* t)
    {
        reset(t);
    }

    void reset(T* t = nullptr)
    {
        value = t;
        version = value ? allocation_from(value)->version : 0;
    }

    Weak& operator=(T* t)
    {
        reset(t);
        return *this;
    }

    // Queries

    bool operator==(const Weak& other) const = default;

    template<typename T2>
    bool operator==(const Weak<T2>& other) const { return value == other.value && version == other.version; };

    explicit operator bool() const { return value && allocation_from(value)->version == version; }
    T*                 get() const { return *this ? value : nullptr; }
    T*          operator->() const { return value; }

    // Conversions

    template<typename T2>
    operator Weak<T2>() { return Weak<T2>{get()}; }
};
