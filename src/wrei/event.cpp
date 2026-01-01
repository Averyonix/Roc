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
    std::array<epoll_event, 16> events;
    while (!loop->stopped) {

        for (auto& prepoll : loop->prepolls) {
            prepoll();
        }

        auto count = wrei_unix_check_n1(epoll_wait(loop->epoll_fd, events.data(), events.size(), -1), EAGAIN);
        if (count < 0) {
            if (errno == EAGAIN) continue;
            return;
        }

        for (i32 i = 0; i < count; ++i) {
            handle_event(events[i]);
        }
    }
}

void wrei_event_loop_add(wrei_event_loop* loop, int fd, u32 events, wrei_event_source* source)
{
    assert(!source->event_loop);
    source->event_loop = loop;

    wrei_unix_check_n1(epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, fd, wrei_ptr_to(epoll_event {
        .events = events,
        .data {
            .ptr = static_cast<wrei_event_source*>(source)
        }
    })));
}

wrei_event_source::~wrei_event_source()
{
    if (event_loop) {
        wrei_unix_check_n1(epoll_ctl(event_loop->epoll_fd, EPOLL_CTL_DEL, fd, nullptr));
    }
}
