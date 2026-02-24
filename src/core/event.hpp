#pragma once

#include "util.hpp"
#include "object.hpp"
#include "fd.hpp"

struct core_event_source;
struct core_fd;

struct core_task
{
    std::move_only_function<void()> callback;
    std::atomic_flag* sync;
};

struct core_event_loop : core_object
{
    bool stopped = false;

    std::thread::id main_thread;

    moodycamel::ConcurrentQueue<core_task> queue;

    u64 tasks_available;
    ref<core_fd> task_fd;

    ref<core_fd> timer_fd;
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

    ~core_event_loop();
};

ref<core_event_loop> core_event_loop_create();
void core_event_loop_run( core_event_loop*);
void core_event_loop_stop(core_event_loop*);

void core_event_loop_timer_expiry_impl(core_event_loop*, std::chrono::steady_clock::time_point exp);

template<typename Lambda>
void core_event_loop_enqueue_timed(core_event_loop* loop, std::chrono::steady_clock::time_point exp, Lambda&& task)
{
    core_assert(std::this_thread::get_id() == loop->main_thread);

    loop->timed_events.emplace_back(exp, std::move(task));

    core_event_loop_timer_expiry_impl(loop, exp);
}

template<typename Lambda>
void core_event_loop_enqueue(core_event_loop* loop, Lambda&& task)
{
    loop->queue.enqueue({ .callback = std::move(task) });
    if (std::this_thread::get_id() == loop->main_thread) {
        loop->tasks_available++;
    } else {
        core_eventfd_signal(loop->task_fd->get(), 1);
    }
}

template<typename Lambda>
void core_event_loop_enqueue_and_wait(core_event_loop* loop, Lambda&& task)
{
    core_assert(std::this_thread::get_id() != loop->main_thread);

    std::atomic_flag done = false;
    // We can avoid moving `task` entirely since its lifetime is guaranteed
    loop->queue.enqueue({ .callback = [&task] { task(); }, .sync = &done });
    core_eventfd_signal(loop->task_fd->get(), 1);
    done.wait(false);
}
