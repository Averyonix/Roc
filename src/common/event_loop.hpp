#pragma once

#include <sys/epoll.h>

struct EventLoop;

using event_loop_fn = void(*)(void*, int, uint32_t);

EventLoop* event_loop_create();
void event_loop_add_fd(EventLoop*, int fd, uint32_t events, event_loop_fn, void* data);
void event_loop_remove_fd(EventLoop*, int fd);
void event_loop_add_post_step(EventLoop*, void(*)(void*), void*);

void event_loop_run(EventLoop*);
