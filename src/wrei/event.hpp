#pragma once

#include "util.hpp"
#include "object.hpp"

struct wrei_event_source;

struct wrei_task
{
    std::move_only_function<void()> callback;
    std::atomic_flag* sync;
};

struct wrei_event_loop : wrei_object
{
    bool stopped = false;

    std::thread::id main_thread;

    moodycamel::ConcurrentQueue<wrei_task> queue;

    u64 tasks_available;
    ref<wrei_event_source> task_source;

    ref<wrei_event_source> timer_source;
    struct timed_event
    {
        std::chrono::steady_clock::time_point expiration;
        std::move_only_function<void()> callback;
    };
    std::deque<timed_event> timed_events;
    std::optional<std::chrono::steady_clock::time_point> current_wakeup;

    int epoll_fd;

    struct {
        u64 events_handled;
        u64 poll_waits;
    } stats;

    ~wrei_event_loop();
};

struct wrei_event_source : wrei_object
{
    weak<wrei_event_loop> event_loop;
    int fd;

    virtual ~wrei_event_source();

    virtual void handle(const epoll_event&) = 0;

    void mark_defunct()
    {
        fd = -1;
    }
};

ref<wrei_event_loop> wrei_event_loop_create();
void wrei_event_loop_run( wrei_event_loop*);
void wrei_event_loop_stop(wrei_event_loop*);
void wrei_event_loop_add( wrei_event_loop*, u32 events, wrei_event_source*);

void wrei_event_loop_timer_expiry_impl(wrei_event_loop*, std::chrono::steady_clock::time_point exp);

template<typename Lambda>
void wrei_event_loop_enqueue_timed(wrei_event_loop* loop, std::chrono::steady_clock::time_point exp, Lambda&& task)
{
    assert(std::this_thread::get_id() == loop->main_thread);

    loop->timed_events.emplace_back(exp, std::move(task));

    wrei_event_loop_timer_expiry_impl(loop, exp);
}

template<typename Lambda>
void wrei_event_loop_enqueue(wrei_event_loop* loop, Lambda&& task)
{
    loop->queue.enqueue({ .callback = std::move(task) });
    if (std::this_thread::get_id() == loop->main_thread) {
        loop->tasks_available++;
    } else {
        wrei_eventfd_signal(loop->task_source->fd, 1);
    }
}

template<typename Lambda>
void wrei_event_loop_enqueue_and_wait(wrei_event_loop* loop, Lambda&& task)
{
    assert(std::this_thread::get_id() != loop->main_thread);

    std::atomic_flag done = false;
    // We can avoid moving `task` entirely since its lifetime is guaranteed
    loop->queue.enqueue({ .callback = [&task] { task(); }, .sync = &done });
    wrei_eventfd_signal(loop->task_source->fd, 1);
    done.wait(false);
}

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
