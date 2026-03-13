#include "debug.hpp"
#include "chrono.hpp"

std::string core_format_with_suffix(std::string_view suffix, f64 amount, u32 decimals)
{
    switch (decimals) {
        case 0: return std::format("{:.0f}{}", amount, suffix);
        case 1: return std::format("{:.1f}{}", amount, suffix);
        case 2: return std::format("{:.2f}{}", amount, suffix);
        default: core_unreachable();
    }
}
