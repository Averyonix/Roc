#pragma once

#include "types.hpp"
#include "log.hpp"
#include "util.hpp"

struct wrei_weak_state
{
    struct wrei_object* value;
};

#define WREI_NOISY_OBJECTS 0
#if WREI_NOISY_OBJECTS
static i64 wrei_debug_global_alive_objects;
#endif

struct wrei_object
{
    u32 _ref_count = 1;
    std::shared_ptr<wrei_weak_state> _weak_state;

#if WREI_NOISY_OBJECTS
    wrei_object()
    {
        log_trace("wrei::object ++ {}", wrei_debug_global_alive_objects++);
    }
#else
    wrei_object() = default;
#endif

    virtual ~wrei_object()
    {
#if WREI_NOISY_OBJECTS
        log_trace("wrei::object -- {}", --wrei_debug_global_alive_objects);
#endif
        if (_weak_state) _weak_state->value = nullptr;
    }


    WREI_DELETE_COPY_MOVE(wrei_object)
};

template<typename T>
T* wrei_add_ref(T* t)
{
    if (t) static_cast<wrei_object*>(t)->_ref_count++;
    return t;
}

template<typename T>
void wrei_remove_ref(T* t)
{
    if (t && !--static_cast<wrei_object*>(t)->_ref_count) {
        delete t;
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

    T*        get() const { return value; }
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

// -----------------------------------------------------------------------------

template<typename T>
struct wrei_weak
{
    std::shared_ptr<wrei_weak_state> _weak_state;

    T*        get() const { return _weak_state ? static_cast<T*>(_weak_state->value) : nullptr; }
    T* operator->() const { return get(); }

    void reset() { _weak_state = {}; }
    wrei_weak& operator=(std::nullptr_t)
    {
        reset();
        return *this;
    }

    operator bool() const { return get(); }

    template<typename T2>
        requires std::derived_from<std::remove_cvref_t<T>, std::remove_cvref_t<T2>>
    operator wrei_weak<T2>() { return wrei_weak<T2>{_weak_state}; }
};

template<typename T>
requires std::derived_from<std::remove_cvref_t<T>, wrei_object>
wrei_weak<T> wrei_weak_from(T* t)
{
    if (!t) return {};
    if (!t->_weak_state) t->_weak_state.reset(new wrei_weak_state{t});
    return wrei_weak<T>{t->_weak_state};
}
