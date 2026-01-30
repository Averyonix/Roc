#include "event.hpp"

ref<wrei_event_loop> wrei_event_loop_create()
{
    auto loop = wrei_create<wrei_event_loop>();
    loop->main_thread = std::this_thread::get_id();

    loop->epoll_fd = epoll_create1(EPOLL_CLOEXEC);

    auto task_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    loop->task_source = wrei_event_loop_add_fd(loop.get(), task_fd, EPOLLIN, [loop = loop.get()](int fd, u32 events) {
        loop->tasks_available += wrei_eventfd_read(fd);

        // Don't double dip task event stats
        loop->stats.events_handled--;
    });

    return loop;
}

wrei_event_loop::~wrei_event_loop()
{
    assert(stopped);

    close(epoll_fd);

    close(task_source->fd);
    task_source->mark_defunct();
}

void wrei_event_loop_stop(wrei_event_loop* loop)
{
    loop->stopped = true;
}

void wrei_event_loop_run(wrei_event_loop* loop)
{
    assert(std::this_thread::get_id() == loop->main_thread);

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
            std::array<weak<wrei_event_source>, max_epoll_events> sources;
            for (i32 i = 0; i < count; ++i) {
                sources[i] = static_cast<wrei_event_source*>(events[i].data.ptr);
            }

            for (i32 i = 0; i < count; ++i) {
                // Check that source is still alive
                if (sources[i]) {
                    loop->stats.events_handled++;
                    sources[i]->handle(events[i]);
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

void wrei_event_loop_add(wrei_event_loop* loop, u32 events, wrei_event_source* source)
{
    assert(!source->event_loop);
    source->event_loop = loop;

    unix_check(epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, source->fd, wrei_ptr_to(epoll_event {
        .events = events,
        .data {
            .ptr = source,
        }
    })));
}

wrei_event_source::~wrei_event_source()
{
    if (event_loop && fd != -1) {
        unix_check(epoll_ctl(event_loop->epoll_fd, EPOLL_CTL_DEL, fd, nullptr));
    }
}
