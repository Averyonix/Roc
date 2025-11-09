#pragma once

#include "common/types.hpp"

#include <sys/epoll.h>

struct EventLoop;

using event_loop_fn = void(*)(void*, int, u32);

EventLoop* event_loop_create();
void event_loop_add_fd(       EventLoop*, int fd, u32 events, event_loop_fn, void* data);
void event_loop_remove_fd(    EventLoop*, int fd);
void event_loop_add_post_step(EventLoop*, void(*)(void*), void*);

void event_loop_run( EventLoop*);
void event_loop_stop(EventLoop*);
