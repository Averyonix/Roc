#pragma once

#include "types.hpp"
#include "util.hpp"
#include "debug.hpp"
#include "hash.hpp"

// -----------------------------------------------------------------------------
//      Memory Registry
// -----------------------------------------------------------------------------

using MemVersion = u32;

struct MemRegion
{
    u32 index;

    [[nodiscard]] explicit constexpr operator bool() const noexcept
    {
        return index;
    }
};

using MemUnregisterFn = void(*)(MemRegion);

// -----------------------------------------------------------------------------

struct MemRegistry
{
    std::flat_map<void*, u32, std::greater<void*>> lookup;

    std::vector<u32> freelist;
    u32 last_index = 0;

    MemVersion last_version = 0;

    u32 slot_count;

    void**           data;
    usz*             size;
    MemVersion*      version;
    u32*             ref_count;
    MemUnregisterFn* unregister;

    MemRegistry();
    ~MemRegistry();
};

extern MemRegistry* mem_registry;

// -----------------------------------------------------------------------------

static
auto mem_get(auto&& field, u32 index) -> decltype(auto)
{
    return field[index];
}

static
auto mem_get(auto&& field, MemRegion region) -> decltype(auto)
{
    return mem_get(field, region.index);
}

#define MEM_GET(Field, Index) mem_get(mem_registry->Field, (Index))

inline
auto mem_register(void* data, usz size, MemUnregisterFn unregister) -> MemRegion
{
    u32 index;
    if (mem_registry->freelist.empty()) {
        index = ++mem_registry->last_index;
    } else {
        index = mem_registry->freelist.back();
        mem_registry->freelist.pop_back();
    }

    MEM_GET(data, index) = data;
    MEM_GET(size, index) = size;
    MEM_GET(version, index) = ++mem_registry->last_version;
    MEM_GET(ref_count, index) = 1;
    MEM_GET(unregister, index) = unregister;

    mem_registry->lookup.emplace(data, index);

    return {index};
}

inline
void mem_unregister(MemRegion region)
{
    debug_assert(region);
    debug_assert(MEM_GET(version, region));
    debug_assert(MEM_GET(ref_count, region) == 0);

    MEM_GET(unregister, region)(region);
    MEM_GET(version, region) = 0;

    mem_registry->lookup.erase(MEM_GET(data, region));

    mem_registry->freelist.emplace_back(region.index);
}

inline
auto mem_get_version(MemRegion region) -> MemVersion
{
    return MEM_GET(version, region);
}

inline
auto mem_ref(MemRegion region) -> u32
{
    if (!region) return 0;

    debug_assert(MEM_GET(version, region));
    return ++MEM_GET(ref_count, region);
}

inline
auto mem_unref(MemRegion region) -> u32
{
    if (!region) return 0;

    debug_assert(MEM_GET(version, region));
    if (!--MEM_GET(ref_count, region)) {
        mem_unregister(region);
        return 0;
    }
    return MEM_GET(ref_count, region);
}

inline
auto mem_get_ref_count(MemRegion region) -> u32&
{
    return MEM_GET(ref_count, region);
}

inline
auto mem_get_data(MemRegion region) -> void*
{
    return MEM_GET(data, region);
}

inline
auto mem_from(void* data) -> MemRegion
{
    if (!data) return {};

    // std::flat_map::lower_bound returns the first entry that is `>= data`
    // We reverse this by using `std::greater` in place of `std::less`
    // So this gives us the last entry that is `<= data`
    auto iter = mem_registry->lookup.lower_bound(data);
    if (iter == mem_registry->lookup.end()) return {};

    MemRegion region = {iter->second};

    // We then bounds check to see if this pointer is contained within the specified region
    uintptr_t lower = uintptr_t(MEM_GET(data, region));
    uintptr_t upper = lower + MEM_GET(size, region);
    if (upper <= uintptr_t(data)) return {};

    debug_assert(MEM_GET(version, region));

    return region;
}

#undef MEM_GET

// -----------------------------------------------------------------------------
//      Dynamic Object Helpers
// -----------------------------------------------------------------------------

template<typename T>
void object_free_impl(MemRegion region)
{
    delete static_cast<T*>(mem_get_data(region));
}

template<typename T>
auto object_create_unsafe(auto&&... args) -> T*
{
    auto obj = new T(std::forward<decltype(args)>(args)...);
    mem_register(obj, sizeof(T), object_free_impl<T>);
    return obj;
}

inline
void object_destroy(void* v)
{
    debug_assert(!mem_unref(mem_from(v)));
}

template<typename T>
auto object_ref(T* t) -> T*
{
    return mem_ref(mem_from(t)) ? t : nullptr;
}

template<typename T>
auto object_unref(T* t) -> T*
{
    return mem_unref(mem_from(t)) ? t : nullptr;
}

// -----------------------------------------------------------------------------
//      Ref Container
// -----------------------------------------------------------------------------

template<typename T>
struct Ref;

template<typename T>
struct Weak;

struct RefAdoptTag {};

template<typename T>
struct Ref
{
    T* value = nullptr;

    void reset(T* t = nullptr)
    {
        if (t == value) return;
        object_unref(value);
        value = object_ref(t);
    }

    // Destruction

    ~Ref()
    {
        object_unref(value);
    }

    void destroy()
    {
        if (value) {
            object_destroy(value);
            value = nullptr;
        }
    }

    // Construction

    Ref() = default;

    Ref(T* t)
        : value(t)
    {
        object_ref(value);
    }

    Ref(T* t, RefAdoptTag)
        : value(t)
    {}

    // Assignment

    Ref(const Ref& other)
        : value(object_ref(other.value))
    {}

    auto& operator=(const Ref& other)
    {
        reset(other.value);
        return *this;
    }

    Ref(Ref&& other)
        : value(std::exchange(other.value, nullptr))
    {}

    auto& operator=(Ref&& other)
    {
        if (value != other.value) {
            object_unref(value);
            value = std::exchange(other.value, nullptr);
        }
        return *this;
    }

    // Queries

    template<typename T2>
    auto operator==(const Ref<T2>& other) const -> bool { return value == other.value; };

    explicit operator bool() const       { return value; }
    auto               get() const -> T* { return value; }
    auto        operator->() const -> T* { return value; }

    // Conversions

    template<typename T2>
    explicit Ref(Weak<T2> other): Ref(other.get()) {}
    explicit Ref(Weak<T>  other): Ref(other.get()) {}

    template<typename T2>
    Ref(Ref<T2> other): Ref(other.value) {}
};

template<typename T>
struct std::hash<Ref<T>>
{
    auto operator()(const Ref<T>& v) -> usz { return hash_single(v.value); }
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
//      Weak Container
// -----------------------------------------------------------------------------

template<typename T>
struct Weak
{
    T* value;
    MemVersion version;

    // Construction

    Weak() = default;

    Weak(T* t)
        : value(t)
        , version(mem_get_version(mem_from(value)))
    {}

    // Queries

    auto operator==(const Weak& other) const -> bool = default;

    template<typename T2>
    auto operator==(const Weak<T2>& other) const -> bool { return value == other.value && version == other.version; };

    explicit operator bool() const       { return value && mem_get_version(mem_from(value)) == version; }
    auto               get() const -> T* { return *this ? value : nullptr; }
    auto        operator->() const -> T* { return value; }

    // Conversions

    template<typename T2>
    Weak(Ref<T2> other): Weak(other.get()) {}
    Weak(Ref<T>  other): Weak(other.get()) {}

    template<typename T2>
    Weak(Weak<T2> other)
        : value(other.value)
        , version(other.version)
    {}
};

template<typename T>
struct std::hash<Weak<T>>
{
    auto operator()(const Weak<T>& v) -> usz { return hash_variadic(v.value, v.version); }
};
