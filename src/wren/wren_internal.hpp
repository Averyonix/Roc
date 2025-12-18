#include "wren.hpp"

// -----------------------------------------------------------------------------

const char* wren_result_to_string(VkResult res);

VkResult wren_check(VkResult res, auto... allowed)
{
    if (res == VK_SUCCESS || (... || (res == allowed))) return res;

    log_error("VULKAN ERROR: {}, ({})", wren_result_to_string(res), int(res));
    std::cout << std::stacktrace::current();

    return res;
}

template<typename Container, typename Fn, typename... Args>
void wren_vk_enumerate(Container& container, Fn&& fn, Args&&... args)
{
    u32 count = static_cast<u32>(container.size());
    for (;;) {
        u32 old_count = count;
        if constexpr (std::same_as<VkResult, decltype(fn(args..., &count, nullptr))>) {
            wren_check(fn(args..., &count, container.data()), VK_INCOMPLETE);
        } else {
            fn(args..., &count, container.data());
        }

        container.resize(count);
        if (count <= old_count) return;
    }
}

inline
auto wren_vk_make_chain_in(std::span<void* const> structures)
{
    VkBaseInStructure* last = nullptr;
    for (auto* s : structures) {
        auto vk_base = static_cast<VkBaseInStructure*>(s);
        vk_base->pNext = last;
        last = vk_base;
    }

    return last;
};

// -----------------------------------------------------------------------------

void wren_register_formats(wren_context*);

// -----------------------------------------------------------------------------

u32 wren_find_vk_memory_type_index(wren_context*, u32 type_filter, VkMemoryPropertyFlags properties);

// -----------------------------------------------------------------------------

void wren_init_descriptors(wren_context*);
void wren_allocate_image_descriptor(wren_image*);
void wren_allocate_sampler_descriptor(wren_sampler*);
