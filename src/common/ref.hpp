#pragma once

#include "types.hpp"
#include "log.hpp"

struct WeakState
{
    struct RefCounted* value;
};

#define NOISY_REF_COUNTS 1
#if NOISY_REF_COUNTS
static i64 debug_global_ref_counted_objects;
#endif

struct RefCounted
{
    u32 ref_count = 1;
    std::shared_ptr<WeakState> weak_state;

#if NOISY_REF_COUNTS
    RefCounted()
    {
        log_trace("RefCounted ++ {}", debug_global_ref_counted_objects++);
    }
#endif

    virtual ~RefCounted()
    {
#if NOISY_REF_COUNTS
        log_trace("RefCounted -- {}", --debug_global_ref_counted_objects);
#endif
    }
};

template<typename T>
T* ref(T* t)
{
    if (!t) return nullptr;
    static_cast<RefCounted*>(t)->ref_count++;
    return t;
}

template<typename T>
void unref(T* t)
{
    if (t && !--static_cast<RefCounted*>(t)->ref_count) {
        delete t;
    }
}

// -----------------------------------------------------------------------------

template<typename T>
struct Ref
{
    T* value;

    Ref() = default;

    ~Ref()
    {
        unref(value);
    }

    Ref(T* t)
        : value(t)
    {
        ref(t);
    }

    Ref& operator=(T* t)
    {
        reset(t);
        return *this;
    }

    void reset(T* t = nullptr)
    {
        if (t == value) return;
        unref(value);
        value = ref(t);
    }

    Ref(const T& other)
        : value(ref(other.value))
    {}

    Ref& operator=(const T& other)
    {
        if (value != other.value) {
            unref(value);
            value = ref(other.value);
        }
        return *this;
    }

    Ref(T&& other)
        : value(std::exchange(other.value))
    {}

    Ref& operator=(T&& other)
    {
        if (value != other.value) {
            unref(value);
            value = std::exchange(other.value);
        }
        return *this;
    }

    operator bool() const { return value; }

    T*        get() const { return value; }
    T* operator->() const { return value; }

    template<typename T2>
        requires std::derived_from<std::remove_cvref_t<T>, std::remove_cvref_t<T2>>
    operator Ref<T2>() { return Ref<T2>(value); }
};

// -----------------------------------------------------------------------------

template<typename T>
struct Weak
{
    std::shared_ptr<WeakState> weak_state;

    T*     get() const { return weak_state ? static_cast<T*>(weak_state->value) : nullptr; }
    void reset()       { weak_state = {}; }

    template<typename T2>
        requires std::derived_from<std::remove_cvref_t<T>, std::remove_cvref_t<T2>>
    operator Weak<T2>() { return Weak<T2>{weak_state}; }
};

template<typename T>
requires std::derived_from<std::remove_cvref_t<T>, RefCounted>
Weak<T> weak_from(T* t)
{
    if (!t) return {};
    if (!t->weak_state) t->weak_state.reset(new WeakState{t});
    return Weak<T>{t->weak_state};
}
