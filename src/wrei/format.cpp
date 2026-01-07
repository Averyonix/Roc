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

std::string wrei_time_to_string(std::chrono::system_clock::time_point time, wrei_time_format format)
{
    tm tm;
    gmtime_r(wrei_ptr_to(std::chrono::system_clock::to_time_t(time)), &tm);

    auto year  = tm.tm_year + 1900;
    auto month = tm.tm_mon + 1;
    auto msec  = std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count() % 1000;

    switch (format) {
        break;case wrei_time_format::iso8601:
            return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z",      year, month, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
        break;case wrei_time_format::datetime_ms:
            return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}", year, month, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, msec);
        break;case wrei_time_format::time:
            return std::format("{:02}:{:02}:{:02}",       tm.tm_hour, tm.tm_min, tm.tm_sec);
        break;case wrei_time_format::time_ms: {
            return std::format("{:02}:{:02}:{:02}.{:03}", tm.tm_hour, tm.tm_min, tm.tm_sec, msec);
        }
    }
}

std::string wrei_duration_to_string(std::chrono::duration<f64, std::nano> dur)
{
    f64 nanos = dur.count();

#define WREI_DURATION_FORMAT_CASE(Size, Suffix) \
    if (nanos >= (Size)) { \
        f64 in_size = nanos / (Size); \
        return print_value_with_suffix(Suffix, in_size, decimals_for_3sf(in_size)); \
    }

    WREI_DURATION_FORMAT_CASE(1e9, "s")
    WREI_DURATION_FORMAT_CASE(1e6, "ms")
    WREI_DURATION_FORMAT_CASE(1e3, "us")
    WREI_DURATION_FORMAT_CASE(1,   "ns")

    return "0";
}

std::string wrei_byte_size_to_string(u64 bytes)
{
#define WREI_BYTE_SIZE_FORMAT_CASE(Size, Suffix) \
    if (bytes >= (Size)) { \
        f64 in_size = f64(bytes) / (Size); \
        return print_value_with_suffix(Suffix, in_size, decimals_for_3sf(in_size)); \
    }

    WREI_BYTE_SIZE_FORMAT_CASE(1ul << 60, "EiB");
    WREI_BYTE_SIZE_FORMAT_CASE(1ul << 50, "PiB");
    WREI_BYTE_SIZE_FORMAT_CASE(1ul << 40, "TiB");
    WREI_BYTE_SIZE_FORMAT_CASE(1ul << 30, "GiB");
    WREI_BYTE_SIZE_FORMAT_CASE(1ul << 20, "MiB");
    WREI_BYTE_SIZE_FORMAT_CASE(1ul << 10, "KiB");

    return std::format("{} byte{}", bytes, bytes == 1 ? "" : "s");
}
