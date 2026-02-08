#include "event.hpp"

static
u32 to_epoll_events(wrei_fd_event_bits events)
{
    u32 out = 0;
    if (events.contains(wrei_fd_event_bit::readable))  out |= EPOLLIN;
    if (events.contains(wrei_fd_event_bit::writable)) out |= EPOLLOUT;
    return out;
}

static
wrei_fd_event_bits from_epoll_events(u32 events)
{
    wrei_fd_event_bits out = {};
    if (events & EPOLLIN)  out |= wrei_fd_event_bit::readable;
    if (events & EPOLLOUT) out |= wrei_fd_event_bit::writable;
    return out;
}

void wrei_event_loop_timer_expiry_impl(wrei_event_loop* loop, std::chrono::steady_clock::time_point exp)
{
    if (loop->current_wakeup && exp > *loop->current_wakeup) {
        // log_error("Earlier timer wakeup already set");
        // log_error("  current expiration: {}", wrei_duration_to_string(*loop->current_wakeup - std::chrono::steady_clock::now()));
        // log_error("  new expiration: {}", wrei_duration_to_string(exp - std::chrono::steady_clock::now()));
        return;
    }

    loop->current_wakeup = exp;

    // log_trace("Next timeout in {}", wrei_duration_to_string(exp - std::chrono::steady_clock::now()));

    unix_check(timerfd_settime(loop->timer_fd->get(), TFD_TIMER_ABSTIME, wrei_ptr_to(itimerspec {
        .it_value = wrei_steady_clock_to_timespec<CLOCK_MONOTONIC>(exp),
    }), nullptr));
}

static
void handle_timer(wrei_event_loop* loop, int fd)
{
    u64 expirations;
    if (unix_check(read(fd, &expirations, sizeof(expirations))).value != sizeof(expirations)) return;

    auto now = std::chrono::steady_clock::now();
    loop->current_wakeup = std::nullopt;

    std::optional<std::chrono::steady_clock::time_point> min_exp;

    std::vector<std::move_only_function<void()>> dequeued;
    std::erase_if(loop->timed_events, [&](auto& event) {
        if (now >= event.expiration) {
            dequeued.emplace_back(std::move(event.callback));
            return true;
        } else {
            min_exp = min_exp ? std::min(*min_exp, event.expiration) : event.expiration;
        }
        return false;
    });

    loop->stats.events_handled += dequeued.size();
    for (auto& callback : dequeued) {
        callback();
    }

    if (min_exp) {
        wrei_event_loop_timer_expiry_impl(loop, *min_exp);
    }
}

ref<wrei_event_loop> wrei_event_loop_create()
{
    auto loop = wrei_create<wrei_event_loop>();
    loop->main_thread = std::this_thread::get_id();

    loop->epoll_fd = unix_check(epoll_create1(EPOLL_CLOEXEC)).value;

    loop->task_fd = wrei_fd_adopt(unix_check(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)).value);
    wrei_fd_set_listener(loop->task_fd.get(), loop.get(), wrei_fd_event_bit::readable, [loop = loop.get()](wrei_fd* fd, wrei_fd_event_bits events) {
        loop->tasks_available += wrei_eventfd_read(fd->get());

        // Don't double dip task event stats
        loop->stats.events_handled--;
    });

    loop->timer_fd = wrei_fd_adopt(unix_check(timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)).value);
    wrei_fd_set_listener(loop->timer_fd.get(), loop.get(), wrei_fd_event_bit::readable, [loop = loop.get()](wrei_fd* fd, wrei_fd_event_bits events) {
        handle_timer(loop, fd->get());

        // Don't double dip timer event stats
        loop->stats.events_handled--;
    });

    loop->internal_listener_count = loop->listener_count;

    return loop;
}

wrei_event_loop::~wrei_event_loop()
{
    wrei_assert(stopped);

    task_fd = nullptr;
    timer_fd = nullptr;

    wrei_assert(listener_count == 0);

    close(epoll_fd);
}

void wrei_event_loop_stop(wrei_event_loop* loop)
{
    loop->stopped = true;

    if (loop->listener_count > loop->internal_listener_count) {
        // Just log an error for now, in the future we will be more strict and
        // assert if any user registered listeners are still attached at this point.
        log_error("Stopping event loop with {} registered listeners remaining!",
            loop->listener_count - loop->internal_listener_count);
    }
}

void wrei_event_loop_run(wrei_event_loop* loop)
{
    wrei_assert(std::this_thread::get_id() == loop->main_thread);

    static constexpr usz max_epoll_events = 64;
    std::array<epoll_event, max_epoll_events> events;

    while (!loop->stopped) {

        // Check for new fd events

        i32 timeout = 0;
        if (!loop->tasks_available) {
            loop->stats.poll_waits++;
            timeout = -1;
        }
        auto[count, error] = unix_check(epoll_wait(loop->epoll_fd, events.data(), events.size(), timeout), EAGAIN, EINTR);
        if (error) {
            if (error == EAGAIN || error == EINTR) {
                if (!loop->tasks_available) continue;
            } else {
                // At this point, we can't assume that we'll receieve any future FD events.
                // Since this includes all user input, the only safe thing to do is
                // immediately terminate to avoid locking out the user's system.
                wrei_debugkill();
            }
        }

        // Flush fd events

        if (count > 0) {
            std::array<ref<wrei_fd>, max_epoll_events> sources = {};
            for (i32 i = 0; i < count; ++i) {
                sources[i] = static_cast<wrei_fd*>(events[i].data.ptr);
            }

            for (i32 i = 0; i < count; ++i) {
                loop->stats.events_handled++;

                auto l = sources[i]->listener.get();
                auto event_bits = from_epoll_events(events[i].events);
                if (l->flags.contains(wrei_fd_listen_flag::oneshot)) {
                    ref listener = l;
                    wrei_fd_remove_listener(sources[i].get());
                    listener->handle(sources[i].get(), event_bits);
                } else {
                    l->handle(sources[i].get(), event_bits);
                }
            }
        }

        // Flush tasks

        if (loop->tasks_available) {
            u64 available = std::exchange(loop->tasks_available, 0);

            loop->stats.events_handled += available;
            for (u64 i = 0; i < available; ++i) {
                wrei_task task;
                while (!loop->queue.try_dequeue(task));

                task.callback();

                if (task.sync) {
                    task.sync->test_and_set();
                    task.sync->notify_one();
                }
            }
        }
    }
}

// -----------------------------------------------------------------------------

void wrei_fd_set_listener(
    wrei_fd* fd,
    wrei_event_loop* loop,
    wrei_fd_listener* listener)
{
    auto events = listener->events;

    wrei_assert(events);
    wrei_assert(!listener->loop);
    wrei_assert(!fd->listener);

    loop->listener_count++;

    listener->loop = loop;
    fd->listener = listener;

    unix_check(epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, fd->get(), wrei_ptr_to(epoll_event {
        .events = to_epoll_events(events),
        .data {
            .ptr = fd,
        }
    })));
}

void wrei_fd_remove_listener(wrei_fd* fd)
{
    fd->listener->loop->listener_count--;

    unix_check(epoll_ctl(fd->listener->loop->epoll_fd, EPOLL_CTL_DEL, fd->get(), nullptr));
    fd->listener->loop = nullptr;
    fd->listener = nullptr;
}
