#pragma once

#include "util.hpp"
#include "object.hpp"

struct wrei_event_source;

struct wrei_task
{
    std::move_only_function<void()> callback;
    std::atomic<bool>* sync;
};

struct wrei_event_loop : wrei_object
{
    bool stopped = false;

    std::thread::id main_thread;

    moodycamel::ConcurrentQueue<wrei_task> queue;
    std::atomic<u32> pending;
    int task_fd;

    int epoll_fd;

    struct {
        u64 events_handled;
    } stats;

    ~wrei_event_loop();
};

struct wrei_event_source : wrei_object
{
    weak<wrei_event_loop> event_loop;
    int fd;

    virtual ~wrei_event_source();

    virtual void handle(const epoll_event&) = 0;
};

ref<wrei_event_loop> wrei_event_loop_create();
void wrei_event_loop_run( wrei_event_loop*);
void wrei_event_loop_stop(wrei_event_loop*);
void wrei_event_loop_add( wrei_event_loop*, u32 events, wrei_event_source*);
void wrei_event_loop_enqueue(wrei_event_loop*, std::move_only_function<void()> task);
void wrei_event_loop_enqueue_and_wait(wrei_event_loop*, std::move_only_function<void()> task);

// -----------------------------------------------------------------------------

template<typename Lambda>
    requires (std::same_as<std::invoke_result_t<Lambda, int, u32>, void>)
struct wrei_event_source_fd : wrei_event_source
{
    Lambda callback;

    wrei_event_source_fd(auto&& callback)
        : wrei_event_source{}
        , callback(std::move(callback))
    {}

    virtual void handle(const epoll_event& event) final override
    {
        callback(fd, event.events);
    }
};

template<typename Lambda>
ref<wrei_event_source> wrei_event_loop_add_fd(wrei_event_loop* loop, int fd, u32 events, Lambda&& callback)
{
    auto source = wrei_create<wrei_event_source_fd<Lambda>>(std::move(callback));
    source->fd = fd;
    wrei_event_loop_add(loop, events, source.get());
    return source;
}
