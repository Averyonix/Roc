#pragma once

#include "types.hpp"
#include "log.hpp"

struct wrei_weak_state
{
    struct wrei_ref_counted* value;
};

#define NOISY_REF_COUNTS 1
#if NOISY_REF_COUNTS
static i64 debug_global_ref_counted_objects;
#endif

struct wrei_ref_counted
{
    u32 ref_count = 1;
    std::shared_ptr<wrei_weak_state> weak_state;

#if NOISY_REF_COUNTS
    wrei_ref_counted()
    {
        log_trace("RefCounted ++ {}", debug_global_ref_counted_objects++);
    }
#endif

    virtual ~wrei_ref_counted()
    {
#if NOISY_REF_COUNTS
        log_trace("RefCounted -- {}", --debug_global_ref_counted_objects);
#endif
    }
};

template<typename T>
T* wrei_add_ref(T* t)
{
    if (!t) return nullptr;
    static_cast<wrei_ref_counted*>(t)->ref_count++;
    return t;
}

template<typename T>
void wrei_remove_ref(T* t)
{
    if (t && !--static_cast<wrei_ref_counted*>(t)->ref_count) {
        delete t;
    }
}

// -----------------------------------------------------------------------------

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

    wrei_ref& operator=(T* t)
    {
        reset(t);
        return *this;
    }

    void reset(T* t = nullptr)
    {
        if (t == value) return;
        wrei_remove_ref(value);
        value = wrei_add_ref(t);
    }

    wrei_ref(const T& other)
        : value(wrei_add_ref(other.value))
    {}

    wrei_ref& operator=(const T& other)
    {
        if (value != other.value) {
            wrei_remove_ref(value);
            value = wrei_add_ref(other.value);
        }
        return *this;
    }

    wrei_ref(T&& other)
        : value(std::exchange(other.value))
    {}

    wrei_ref& operator=(T&& other)
    {
        if (value != other.value) {
            wrei_remove_ref(value);
            value = std::exchange(other.value);
        }
        return *this;
    }

    operator bool() const { return value; }

    T*        get() const { return value; }
    T* operator->() const { return value; }

    template<typename T2>
        requires std::derived_from<std::remove_cvref_t<T>, std::remove_cvref_t<T2>>
    operator wrei_ref<T2>() { return wrei_ref<T2>(value); }
};

// -----------------------------------------------------------------------------

template<typename T>
struct wrei_weak
{
    std::shared_ptr<wrei_weak_state> weak_state;

    T*     get() const { return weak_state ? static_cast<T*>(weak_state->value) : nullptr; }
    void reset()       { weak_state = {}; }

    template<typename T2>
        requires std::derived_from<std::remove_cvref_t<T>, std::remove_cvref_t<T2>>
    operator wrei_weak<T2>() { return wrei_weak<T2>{weak_state}; }
};

template<typename T>
requires std::derived_from<std::remove_cvref_t<T>, wrei_ref_counted>
wrei_weak<T> wrei_weak_from(T* t)
{
    if (!t) return {};
    if (!t->weak_state) t->weak_state.reset(new wrei_weak_state{t});
    return wrei_weak<T>{t->weak_state};
}
