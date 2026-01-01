#include "util.hpp"
#include "object.hpp"

struct wrei_event_source;

struct wrei_event_loop : wrei_object
{
    int epoll_fd;
    bool stopped = false;

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

// -----------------------------------------------------------------------------

struct wrei_event_source_tasks : wrei_event_source
{
    moodycamel::ConcurrentQueue<std::function<void()>> tasks;

    ~wrei_event_source_tasks();

    virtual void handle(const epoll_event& event) final override;
};

ref<wrei_event_source_tasks> wrei_event_loop_add_tasks(wrei_event_loop*);

/**
  * Enqueue a task to run in the main event thread
  *
  * This function can be run from *any* thread
  */
void wrei_event_source_tasks_enqueue(wrei_event_source_tasks*, std::function<void()> task);
