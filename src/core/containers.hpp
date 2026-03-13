#pragma once

#include "types.hpp"

// -----------------------------------------------------------------------------
//      Enum Map
// -----------------------------------------------------------------------------

template<typename E, typename T>
struct core_enum_map
{
    T _data[magic_enum::enum_count<E>()];

    static constexpr auto enum_values = magic_enum::enum_values<E>();

    constexpr       T& operator[](E value)       { return _data[magic_enum::enum_index(value).value()]; }
    constexpr const T& operator[](E value) const { return _data[magic_enum::enum_index(value).value()]; }
};

// -----------------------------------------------------------------------------
//      Counting Set
// -----------------------------------------------------------------------------

template<typename T>
struct core_counting_set
{
    using value_type = T;

    std::flat_map<T, u32> counts;

    bool inc(auto&& t)
    {
        return !counts[t]++;
    }

    bool dec(auto&& t)
    {
        auto iter = counts.find(t);
        core_assert(iter != counts.end());
        if (!--iter->second) {
            counts.erase(iter);
            return true;
        }
        return false;
    }

    auto begin() const { return counts.keys().begin(); }
    auto   end() const { return counts.keys().end();   }

    bool contains(const T& t) const { return counts.contains(t); }
    usz      size()           const { return counts.size();      }
    bool    empty()           const { return counts.empty();     }
};

// -----------------------------------------------------------------------------
//      Fixed Array
// -----------------------------------------------------------------------------

template<typename T, u32 Max>
struct core_fixed_array {
    std::array<T, Max> data = {};
    u32 count = 0;

    auto begin(this auto&& self) { return self.data.begin();              }
    auto   end(this auto&& self) { return self.data.begin() + self.count; }

    auto& operator[](this auto&& self, usz i) { return self.data[i]; }
};

// -----------------------------------------------------------------------------
//      Intrusive Linked List
// -----------------------------------------------------------------------------

template<typename Base>
struct core_intrusive_list_base
{
    core_intrusive_list_base* next = this;
    core_intrusive_list_base* prev = this;
};

template<typename Base>
struct core_intrusive_list_iterator
{
    core_intrusive_list_base<Base>* cur;

    void insert_after(core_intrusive_list_base<Base>* base)
    {
        base->prev = cur;
        base->next = cur->next;

        cur->next->prev = base;
        cur->next = base;
    }

    core_intrusive_list_iterator remove()
    {
        cur->next->prev = cur->prev;
        cur->prev->next = cur->next;

        cur->next = cur;
        cur->prev = cur;

        return *this;
    }

    Base* operator->() { return get(); }
    Base* get() { return static_cast<Base*>(cur); }

    bool operator==(const core_intrusive_list_iterator&) const noexcept = default;

    core_intrusive_list_iterator next() { return {cur->next}; }
    core_intrusive_list_iterator prev() { return {cur->prev}; }
};

template<typename Base>
struct core_intrusive_list
{
    using iterator = core_intrusive_list_iterator<Base>;

    core_intrusive_list_base<Base> root;

    iterator first() { return {root.next}; }
    iterator last()  { return {root.prev}; }
    iterator end()   { return {&root};      }

    bool empty() const { return root.next == &root; }
};
