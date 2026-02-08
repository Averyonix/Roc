#pragma once

#include "util.hpp"
#include "object.hpp"
#include "fd.hpp"

struct wrei_event_source;
struct wrei_fd;

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
    ref<wrei_fd> task_fd;

    ref<wrei_fd> timer_fd;
    struct timed_event
    {
        std::chrono::steady_clock::time_point expiration;
        std::move_only_function<void()> callback;
    };
    std::deque<timed_event> timed_events;
    std::optional<std::chrono::steady_clock::time_point> current_wakeup;

    u32 internal_listener_count;
    u32 listener_count = 0;

    int epoll_fd;

    struct {
        u64 events_handled;
        u64 poll_waits;
    } stats;

    ~wrei_event_loop();
};

ref<wrei_event_loop> wrei_event_loop_create();
void wrei_event_loop_run( wrei_event_loop*);
void wrei_event_loop_stop(wrei_event_loop*);

void wrei_event_loop_timer_expiry_impl(wrei_event_loop*, std::chrono::steady_clock::time_point exp);

template<typename Lambda>
void wrei_event_loop_enqueue_timed(wrei_event_loop* loop, std::chrono::steady_clock::time_point exp, Lambda&& task)
{
    wrei_assert(std::this_thread::get_id() == loop->main_thread);

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
        wrei_eventfd_signal(loop->task_fd->get(), 1);
    }
}

template<typename Lambda>
void wrei_event_loop_enqueue_and_wait(wrei_event_loop* loop, Lambda&& task)
{
    wrei_assert(std::this_thread::get_id() != loop->main_thread);

    std::atomic_flag done = false;
    // We can avoid moving `task` entirely since its lifetime is guaranteed
    loop->queue.enqueue({ .callback = [&task] { task(); }, .sync = &done });
    wrei_eventfd_signal(loop->task_fd->get(), 1);
    done.wait(false);
}
