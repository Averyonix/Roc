#pragma once

#include "../util.hpp"

#include <core/id.hpp>

// -----------------------------------------------------------------------------

DECLARE_TAGGED_INTEGER(WayCommitId, u32);

// -----------------------------------------------------------------------------

enum class WaySurfaceRole : u32
{
    none,
    cursor,
    drag_icon,
    subsurface,
    xdg_toplevel,
    xdg_popup,
};

// -----------------------------------------------------------------------------

struct WaySurface;

struct WaySurfaceAddon
{
    WaySurface* surface;

    virtual void commit(WayCommitId) = 0;
    virtual auto test(  WayCommitId) -> bool { return true; };
    virtual void apply( WayCommitId) = 0;

    ~WaySurfaceAddon();
};

// -----------------------------------------------------------------------------

template<typename T>
struct WayCommitQueue
{
    Ref<T> pending;
    std::deque<Ref<T>> queue;

    WayCommitQueue()
        : pending(ref_create<T>())
    {}

    void commit(WayCommitId id)
    {
        pending->commit = id;
        queue.emplace_back(std::move(pending));
        pending = ref_create<T>();
    }

    T* peek(WayCommitId id)
    {
        if (queue.empty()) return {};
        if (queue.front()->commit != id) {
            debug_assert(queue.front()->commit > id,
                "Unexpected commit in queue with id {} attempting to peek {}", queue.front()->commit.value, id.value);
            return {};
        }

        return queue.front().get();
    }

    Ref<T> dequeue(WayCommitId id)
    {
        auto* front = peek(id);
        if (!front) return nullptr;

        Ref ref = front;
        queue.pop_front();
        return ref;
    }
};
