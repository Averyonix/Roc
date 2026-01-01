#pragma once

#include "wrei/types.hpp"
#include "wrei/log.hpp"
#include "wrei/util.hpp"

// -----------------------------------------------------------------------------

#define WROC_STUB(Member)       .Member = [](auto...) { log_warn("TODO: " #Member); }
#define WROC_STUB_QUIET(Member) .Member = [](auto...) { }

// -----------------------------------------------------------------------------

#define wroc_send(Fn, Resource, ...) \
    wroc_send_impl(#Fn, Fn, Resource __VA_OPT__(,) __VA_ARGS__)

void wroc_queue_client_flush();

void wroc_send_impl(const char* fn_name, auto fn, auto&& resource, auto&&... args)
{
    if (resource) {
        fn(resource, args...);
        wroc_queue_client_flush();
    } else {
        log_error("Failed to dispatch {}, resource is null", fn_name);
    }
}

// -----------------------------------------------------------------------------

template<typename T>
std::span<T> wroc_to_span(wl_array* array)
{
    usz count = array->size / sizeof(T);
    return std::span<T>(static_cast<T*>(array->data), count);
}

template<typename T>
wl_array wroc_to_wl_array(std::span<T> span)
{
    return wl_array {
        .size = span.size_bytes(),
        .alloc = span.size_bytes(),
        .data = const_cast<void*>(static_cast<const void*>(span.data())),
    };
}

// -----------------------------------------------------------------------------

inline
wl_client* wroc_resource_get_client(wl_resource* resource)
{
    return resource ? wl_resource_get_client(resource) : nullptr;
}

// -----------------------------------------------------------------------------

class wroc_resource
{
    wl_resource* resource = {};
    wl_listener destroy_listener {
        .notify = on_destroy,
    };

public:
    wroc_resource()
    {
        wl_list_init(&destroy_listener.link);
    }

    wroc_resource(wl_resource* resource)
        : resource(resource)
    {
        wl_resource_add_destroy_listener(resource, &destroy_listener);
    }

    void reset(wl_resource* new_resource)
    {
        if (resource == new_resource) return;

        resource = new_resource;
        wl_list_remove(&destroy_listener.link);
        wl_list_init(&destroy_listener.link);

        if (resource) {
            wl_resource_add_destroy_listener(new_resource, &destroy_listener);
        }
    }

    wroc_resource& operator=(wl_resource* other)
    {
        reset(other);
        return *this;
    }

    static void on_destroy(wl_listener* listener, void* data)
    {
        wroc_resource* self = wl_container_of(listener, self, destroy_listener);
        self->resource = nullptr;
        wl_list_init(&self->destroy_listener.link);

        // log_debug("wrei_resource<{}>: resource destroyed, clearing..", (void*)self);
    }

    ~wroc_resource()
    {
        wl_list_remove(&destroy_listener.link);
    }

    WREI_DELETE_COPY_MOVE(wroc_resource)

    operator wl_resource*() const { return resource; }
};

class wroc_resource_list
{
    struct list_node
    {
        wl_resource* resource = nullptr;
        wl_listener destroy_listener {
            .notify = on_destroy,
        };
        list_node* prev = nullptr;
        list_node* next = nullptr;

        list_node()
        {
            wl_list_init(&destroy_listener.link);
        }

        list_node(wl_resource* resource)
            : resource(resource)
        {
            wl_resource_add_destroy_listener(resource, &destroy_listener);
        }

        static void on_destroy(wl_listener* listener, void* data)
        {
            list_node* self = wl_container_of(listener, self, destroy_listener);

            // log_debug("cleaning up list_node: {}", (void*)self);
            wl_list_init(&self->destroy_listener.link);

            if (self->prev) self->prev->next = self->next;
            if (self->next) self->next->prev = self->prev;

            delete self;
        }

        ~list_node()
        {
            // log_debug("List node destroyed: {}", (void*)this);

            wl_list_remove(&destroy_listener.link);
        }

        WREI_DELETE_COPY_MOVE(list_node)
    };

    list_node root;

    struct iterator
    {
        const list_node* current;

        iterator& operator++()
        {
            current = current->next;
            return *this;
        }

        bool operator==(const iterator& other) const
        {
            return current == other.current;
        }

        wl_resource* operator*() const
        {
            return current->resource;
        }
    };

public:
    wroc_resource_list()
    {
        root.next = &root;
        root.prev = &root;
    }

    void emplace_back(wl_resource* resource)
    {
        if (!resource) return;

        auto* node = new list_node{resource};

        node->next = &root;
        node->prev = root.prev;

        root.prev->next = node;
        root.prev = node;
    }

    void clear()
    {
        list_node* next = nullptr;
        for (auto* node = root.next; node != &root; node = next) {
            next = node->next;
            delete node;
        }
        root.next = &root;
        root.prev = &root;
    }

    void take_and_append_all(wroc_resource_list&& other)
    {
        if (other.root.next == &other.root) return;

        other.root.next->prev = root.prev;
        other.root.prev->next = &root;

        root.prev->next = other.root.next;
        root.prev       = other.root.prev;

        other.root.next = &other.root;
        other.root.prev = &other.root;
    }

    wl_resource* front() const
    {
        return root.next ? root.next->resource : nullptr;
    }

    iterator begin() const
    {
        return iterator{root.next};
    }

    iterator end() const
    {
        return iterator{&root};
    }

    ~wroc_resource_list()
    {
        clear();
    }

    WREI_DELETE_COPY_MOVE(wroc_resource_list)
};

// -----------------------------------------------------------------------------

bool wroc_is_client_behind(wl_client* client);
