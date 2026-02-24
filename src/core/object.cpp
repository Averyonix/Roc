#include "object.hpp"

wrei_registry::~wrei_registry()
{
    if (active_allocations) {
        log_error("Registry found {} remaining active allocations", active_allocations);
    }

    usz total_allocation_size = 0;

    for (auto[i, bin] : bins | std::views::enumerate) {
        total_allocation_size += bin.size() * (usz(1) << i);
        if (!bin.empty()) {
            log_debug("Registry cleaning up {} allocations from bin size: {}", bin.size(), 1 << i);
        }
        for (auto* header : bin) {
            ::free(header);
        }
    }

    log_debug("Peak registry allocation: {}", wrei_byte_size_to_string(total_allocation_size));
}

auto wrei_registry::allocate(usz size) -> wrei_allocation_header*
{
    size = wrei_round_up_power2(size + sizeof(wrei_allocation_header));

    active_allocations++;

    u8 bin_idx = std::countr_zero(size);
    auto& bin = bins[bin_idx];

    wrei_allocation_header* header;
    if (bin.empty()) {
        header = static_cast<wrei_allocation_header*>(::malloc(size));
        new (header) wrei_allocation_header {
            .bin = bin_idx,
        };
        header->version = 1;
    } else {
        header = bin.back();
        bin.pop_back();
        inactive_allocations--;
    }

    header->ref_count = 1;

    return header;
}

void wrei_registry::free(wrei_allocation_header* header)
{
    active_allocations--;
    inactive_allocations++;

    header->version++;

    bins[header->bin].emplace_back(header);
}

struct wrei_registry wrei_registry;
