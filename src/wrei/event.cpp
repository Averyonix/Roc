#include "event.hpp"

ref<wrei_event_loop> wrei_event_loop_create()
{
    auto loop = wrei_create<wrei_event_loop>();

    loop->epoll_fd = epoll_create1(EPOLL_CLOEXEC);

    loop->task_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    wrei_unix_check_n1(epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, loop->task_fd, wrei_ptr_to(epoll_event {
        .events = EPOLLIN,
        .data { .ptr = nullptr, }
    })));

    return loop;
}

wrei_event_loop::~wrei_event_loop()
{
    close(epoll_fd);
}

void wrei_event_loop_stop(wrei_event_loop* loop)
{
    loop->stopped = true;
}

void wrei_event_loop_run(wrei_event_loop* loop)
{
    loop->main_thread = std::this_thread::get_id();

    static constexpr usz event_buffer_count = 64;

    std::array<epoll_event, event_buffer_count> events;
    while (!loop->stopped) {
        auto count = wrei_unix_check_n1(epoll_wait(loop->epoll_fd, events.data(), events.size(), -1), EAGAIN, EINTR);
        if (count < 0) {
            if (errno == EAGAIN || errno == EINTR) continue;

            // At this point, we can't assume that we'll receieve any future FD events.
            // Since this includes all user input, the only safe thing to do is immediately terminate
            // to avoid locking out the user's system.
            std::terminate();
        }

        {
            std::array<weak<wrei_event_source>, event_buffer_count> sources;
            for (i32 i = 0; i < count; ++i) {
                sources[i] = static_cast<wrei_event_source*>(events[i].data.ptr);
            }

            for (i32 i = 0; i < count; ++i) {
                // Check that source is still alive
                //   (also incidentally handles null source events used to wake up the loop)
                if (sources[i]) {
                    loop->stats.events_handled++;
                    sources[i]->handle(events[i]);
                }
            }
        }

        if (loop->pending) {
            u64 tasks = 0;
            wrei_unix_check_n1(read(loop->task_fd, &tasks, sizeof(tasks)), EAGAIN);

            for (u64 i = 0; i < tasks; ++i)
            {
                wrei_task task;
                while (!loop->queue.try_dequeue(task));

                loop->pending--;
                loop->stats.events_handled++;

                task.callback();

                if (task.sync) {
                    *task.sync = true;
                    task.sync->notify_one();
                }
            }
        }
    }
}

static
void enqueue(wrei_event_loop* loop, std::move_only_function<void()>&& task, std::atomic<bool>* sync)
{
    loop->queue.enqueue({ .callback = std::move(task), .sync = sync });
    loop->pending++;

    u64 inc = 1;
    wrei_unix_check_n1(write(loop->task_fd, &inc, sizeof(inc)));
}

void wrei_event_loop_enqueue(wrei_event_loop* loop, std::move_only_function<void()> task)
{
    enqueue(loop, std::move(task), nullptr);
}

void wrei_event_loop_enqueue_and_wait(wrei_event_loop* loop, std::move_only_function<void()> task)
{
    assert(std::this_thread::get_id() != loop->main_thread);

    std::atomic<bool> done = false;
    enqueue(loop, std::move(task), &done);
    done.wait(false);
}

void wrei_event_loop_add(wrei_event_loop* loop, u32 events, wrei_event_source* source)
{
    assert(!source->event_loop);
    source->event_loop = loop;

    wrei_unix_check_n1(epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, source->fd, wrei_ptr_to(epoll_event {
        .events = events,
        .data {
            .ptr = source,
        }
    })));
}

wrei_event_source::~wrei_event_source()
{
    if (event_loop && fd != -1) {
        wrei_unix_check_n1(epoll_ctl(event_loop->epoll_fd, EPOLL_CTL_DEL, fd, nullptr));
    }
}
