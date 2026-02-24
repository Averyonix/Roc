#include "object.hpp"

core_registry::~core_registry()
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

    log_debug("Peak registry allocation: {}", core_byte_size_to_string(total_allocation_size));
}

auto core_registry::allocate(usz size) -> core_allocation_header*
{
    size = core_round_up_power2(size + sizeof(core_allocation_header));

    active_allocations++;

    u8 bin_idx = std::countr_zero(size);
    auto& bin = bins[bin_idx];

    core_allocation_header* header;
    if (bin.empty()) {
        header = static_cast<core_allocation_header*>(::malloc(size));
        new (header) core_allocation_header {
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

void core_registry::free(core_allocation_header* header)
{
    active_allocations--;
    inactive_allocations++;

    header->version++;

    bins[header->bin].emplace_back(header);
}

struct core_registry core_registry;
