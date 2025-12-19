#include "object.hpp"

wrei_registry::~wrei_registry()
{
    if (active_allocations) {
        log_error("Registry found {} remaining active allocations", active_allocations);
        wrei_debugbreak();
    }

    for (auto[i, bin] : bins | std::views::enumerate) {
        if (!bin.empty()) {
            log_debug("Registry cleaning up {} allocations from bin size: {}", bin.size(), 1 << i);
        }
        for (auto& block : bin) {
            ::free(block.data);
        }
    }
}

auto wrei_registry::allocate(usz size) -> allocated_block
{
    assert(std::popcount(size) == 1);

    active_allocations++;
    lifetime_allocations++;

    allocated_block block;

    auto bin_idx = std::countr_zero(size);
    auto& bin = bins[bin_idx];

    // log_trace("allocate({}), bin[{}].count = {}", size, bin_idx, bin.size());

    if (bin.empty()) {
        block.data = ::malloc(size);
        block.version = 1;
    } else {
        block = bin.back();
        bin.pop_back();
        inactive_allocations--;
    }

    return block;
}

void wrei_registry::free(wrei_object* object, wrei_object_version version)
{
    assert(version == object->wrei.version);

    version++;
    assert(version != 0);

    active_allocations--;
    inactive_allocations++;

    auto size = object->wrei.size;

    object->~wrei_object();
    new (object) wrei_object {};

    auto& bin = bins[std::countr_zero(size)];
    bin.emplace_back(object, version);
}

struct wrei_registry wrei_registry;
