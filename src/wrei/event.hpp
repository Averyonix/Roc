#include "util.hpp"
#include "object.hpp"

struct wrei_event_source;

struct wrei_event_loop : wrei_object
{
    int epoll_fd;
    bool stopped = false;

    std::vector<std::function<void()>> prepolls;

    ~wrei_event_loop();
};

struct wrei_event_source : wrei_object
{
    weak<wrei_event_loop> event_loop;
    int fd;

    wrei_event_source(int fd)
        : event_loop()
        , fd(fd)
    {}

    virtual ~wrei_event_source();

    virtual void handle(const epoll_event&) = 0;
};

ref<wrei_event_loop> wrei_event_loop_create();
void wrei_event_loop_run( wrei_event_loop*);
void wrei_event_loop_stop(wrei_event_loop*);
void wrei_event_loop_add( wrei_event_loop*, int fd, u32 events, wrei_event_source*);

// -----------------------------------------------------------------------------

template<typename Lambda>
    requires (std::same_as<std::invoke_result_t<Lambda, int, u32>, void>)
struct wrei_event_source_fd : wrei_event_source
{
    Lambda callback;

    wrei_event_source_fd(int fd, auto&& callback)
        : wrei_event_source(fd)
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
    auto source = wrei_create<wrei_event_source_fd<Lambda>>(fd, std::move(callback));
    wrei_event_loop_add(loop, fd, events, source.get());
    return source;
}
