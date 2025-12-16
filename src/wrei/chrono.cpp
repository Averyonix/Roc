#include "util.hpp"

// -----------------------------------------------------------------------------

static
u32 decimals_for_3sf(f64 value)
{
    if (value < 10) return 2;
    if (value < 100) return 1;
    return 0;
}

static
std::string print_value_with_suffix(std::string_view suffix, f64 amount, u32 decimals)
{
    switch (decimals) {
        case 0: return std::format("{:.0f}{}", amount, suffix);
        case 1: return std::format("{:.1f}{}", amount, suffix);
        case 2: return std::format("{:.2f}{}", amount, suffix);
        default: std::unreachable();
    }
}

std::string wrei_duration_to_string(std::chrono::duration<f64, std::nano> dur)
{
    f64 nanos = dur.count();

    if (nanos >= 1e9) {
        f64 seconds = nanos / 1e9;
        return print_value_with_suffix("s", seconds, decimals_for_3sf(seconds));
    } else if (nanos >= 1e6) {
        f64 millis = nanos / 1e6;
        return print_value_with_suffix("ms", millis, decimals_for_3sf(millis));
    } else if (nanos >= 1e3) {
        f64 micros = nanos / 1e3;
        return print_value_with_suffix("us", micros, decimals_for_3sf(micros));
    } else if (nanos >= 0) {
        return print_value_with_suffix("ns", nanos, decimals_for_3sf(nanos));
    } else {
        return "0";
    }
}
