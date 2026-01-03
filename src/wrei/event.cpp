#include "event.hpp"

ref<wrei_event_loop> wrei_event_loop_create()
{
    auto loop = wrei_create<wrei_event_loop>();

    loop->epoll_fd = epoll_create1(EPOLL_CLOEXEC);

    return loop;
}

wrei_event_loop::~wrei_event_loop()
{
    close(epoll_fd);
}

static
void handle_event(const epoll_event& event)
{
    auto* source = static_cast<wrei_event_source*>(event.data.ptr);
    source->handle(event);
}

void wrei_event_loop_stop(wrei_event_loop* loop)
{
    loop->stopped = true;
}

void wrei_event_loop_run(wrei_event_loop* loop)
{
    std::array<epoll_event, 64> events;
    while (!loop->stopped) {
        auto count = wrei_unix_check_n1(epoll_wait(loop->epoll_fd, events.data(), events.size(), -1), EAGAIN);
        if (count < 0) {
            if (errno == EAGAIN) continue;
            return;
        }
        loop->stats.events_handled += count;

        for (i32 i = 0; i < count; ++i) {
            handle_event(events[i]);
        }
    }
}

void wrei_event_loop_add(wrei_event_loop* loop, u32 events, wrei_event_source* source)
{
    assert(!source->event_loop);
    source->event_loop = loop;

    wrei_unix_check_n1(epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, source->fd, wrei_ptr_to(epoll_event {
        .events = events,
        .data {
            .ptr = static_cast<wrei_event_source*>(source)
        }
    })));
}

wrei_event_source::~wrei_event_source()
{
    if (event_loop && fd >= 0) {
        wrei_unix_check_n1(epoll_ctl(event_loop->epoll_fd, EPOLL_CTL_DEL, fd, nullptr));
    }
}

// -----------------------------------------------------------------------------

wrei_event_source_tasks::~wrei_event_source_tasks()
{
    close(fd);
    fd = -1;
}

void wrei_event_source_tasks::handle(const epoll_event& event)
{
    u64 count;
    auto res = wrei_unix_check_n1(read(fd, &count, sizeof(count)), EAGAIN);
    if (res <= 0) return;

    std::vector<std::function<void()>> dequeued(count);
    usz num_dequeued = 0;
    while (num_dequeued < count) {
        num_dequeued += tasks.try_dequeue_bulk(dequeued.begin() + num_dequeued, count - num_dequeued);
    }

    for (auto& task : dequeued) {
        task();
    }
}

ref<wrei_event_source_tasks> wrei_event_loop_add_tasks(wrei_event_loop* loop)
{
    auto source = wrei_create<wrei_event_source_tasks>();
    source->fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);

    wrei_event_loop_add(loop, EPOLLIN, source.get());

    return source;
}

void wrei_event_source_tasks_enqueue(wrei_event_source_tasks* tasks, std::function<void()> task)
{
    tasks->tasks.enqueue(std::move(task));

    usz count = 1;
    wrei_unix_check_n1(write(tasks->fd, &count, sizeof(count)));
}
