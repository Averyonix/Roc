#include "event_loop.hpp"

#include "util.hpp"

#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

struct EventSource
{
    int fd;
    event_loop_fn callback;
    void* data;
};

struct EventLoopPostStep
{
    void(*fn)(void*);
    void* data;
};

struct EventLoop
{
    int epoll_fd = -1;
    std::list<EventSource> handlers;
    std::vector<EventLoopPostStep> post_steps;
};

EventLoop* event_loop_create()
{
    auto* event_loop = new EventLoop{};

    event_loop->epoll_fd = epoll_create1(0);

    return event_loop;
}

void event_loop_add_fd(EventLoop* event_loop, int fd, u32 events, event_loop_fn callback, void* data)
{
    epoll_event event {
        .events = events,
        .data {
            .ptr = &event_loop->handlers.emplace_back(fd, callback, data),
        },
    };
    unix_check_n1(epoll_ctl(event_loop->epoll_fd, EPOLL_CTL_ADD, fd, &event));
}

void event_loop_remove_fd(EventLoop* event_loop, int fd)
{
    std::erase_if(event_loop->handlers, [&](auto& handler) {
        return handler.fd == fd;
    });
    unix_check_n1(epoll_ctl(event_loop->epoll_fd, EPOLL_CTL_DEL, fd, nullptr));
}

void event_loop_add_post_step(EventLoop* event_loop, void(*fn)(void*), void* data)
{
    event_loop->post_steps.emplace_back(fn, data);
}

void event_loop_run(EventLoop* event_loop)
{
    for (auto& post : event_loop->post_steps) {
        post.fn(post.data);
    }

    for (;;) {
        epoll_event events[16];
        auto events_ready = unix_check_n1(epoll_wait(event_loop->epoll_fd, events, std::size(events), -1), EINTR, EBADF);
        if (events_ready < 0) {
            if (errno == EBADF) {
                log_info("Closing event loop");
                break;
            }
            continue;
        }

        for (int i = 0; i < events_ready; ++i) {
            auto* handler = static_cast<EventSource*>(events[i].data.ptr);
            handler->callback(handler->data, handler->fd, events[i].events);
        }

        for (auto& post : event_loop->post_steps) {
            post.fn(post.data);
        }
    }
}

void event_loop_stop(EventLoop* event_loop)
{
    close(event_loop->epoll_fd);
    event_loop->epoll_fd = -1;
}
