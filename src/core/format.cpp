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
        default: core_unreachable();
    }
}

std::string core_time_to_string(std::chrono::system_clock::time_point time, core_time_format format)
{
    tm tm = {};
    gmtime_r(ptr_to(std::chrono::system_clock::to_time_t(time)), &tm);

    auto year  = tm.tm_year + 1900;
    auto month = tm.tm_mon + 1;
    auto msec  = std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count() % 1000;

    static constexpr const char* weekdays[] = {
        "Sunday",
        "Monday",
        "Tuesday",
        "Wednesday",
        "Thursday",
        "Friday",
        "Saturday",
    };

    static constexpr const char* months[] = {
        "January",
        "February",
        "March",
        "April",
        "May",
        "June",
        "July",
        "August",
        "September",
        "October",
        "November",
        "December"
    };

    static constexpr auto day_suffix = [&](int i) {
        switch (i) {
            break;case 1: case 21: case 31: return "st";
            break;case 2: case 22:          return "nd";
            break;case 3: case 23:          return "rd";
            break;default:                  return "th";
        }
    };

    switch (format) {
        break;case core_time_format::date_pretty:
            return std::format("{}, {} {}{}", weekdays[tm.tm_wday], months[tm.tm_mon], tm.tm_mday, day_suffix(tm.tm_mday));

        break;case core_time_format::iso8601:
            return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z",      year, month, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
        break;case core_time_format::datetime:
            return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}",       year, month, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
        break;case core_time_format::datetime_ms:
            return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}", year, month, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, msec);

        break;case core_time_format::time:
            return std::format("{:02}:{:02}:{:02}",       tm.tm_hour, tm.tm_min, tm.tm_sec);
        break;case core_time_format::time_ms: {
            return std::format("{:02}:{:02}:{:02}.{:03}", tm.tm_hour, tm.tm_min, tm.tm_sec, msec);
        }
    }
}

std::string core_duration_to_string(std::chrono::duration<f64, std::nano> dur)
{
    f64 nanos = dur.count();

    if (nanos >= 60 * 1e9) {
        if (nanos >= 3600 * 1e9) {
            f64 secs = nanos / 1e9;
            f64 hours = std::floor(secs / 3600);
            secs -= hours * 3600;
            f64 mins = std::floor(secs / 60);
            secs -= mins * 60;
            return std::format("{:.0f}h {:.0f}m {:.0f}s", hours, mins, secs);
        }
        f64 secs = nanos / 1e9;
        f64 mins = std::floor(secs / 60);
        secs -= mins * 60;
        return std::format("{:.0f}m {:.1f}s", mins, secs);
    }

#define CORE_DURATION_FORMAT_CASE(Size, Suffix) \
    if (nanos >= (Size)) { \
        f64 in_size = nanos / (Size); \
        return print_value_with_suffix(Suffix, in_size, decimals_for_3sf(in_size)); \
    }

    CORE_DURATION_FORMAT_CASE(1e9, "s")
    CORE_DURATION_FORMAT_CASE(1e6, "ms")
    CORE_DURATION_FORMAT_CASE(1e3, "us")
    CORE_DURATION_FORMAT_CASE(1,   "ns")

    return "0";
}

std::string core_byte_size_to_string(u64 bytes)
{
#define CORE_BYTE_SIZE_FORMAT_CASE(Size, Suffix) \
    if (bytes >= (Size)) { \
        f64 in_size = f64(bytes) / (Size); \
        return print_value_with_suffix(Suffix, in_size, decimals_for_3sf(in_size)); \
    }

    CORE_BYTE_SIZE_FORMAT_CASE(1ul << 60, "EiB");
    CORE_BYTE_SIZE_FORMAT_CASE(1ul << 50, "PiB");
    CORE_BYTE_SIZE_FORMAT_CASE(1ul << 40, "TiB");
    CORE_BYTE_SIZE_FORMAT_CASE(1ul << 30, "GiB");
    CORE_BYTE_SIZE_FORMAT_CASE(1ul << 20, "MiB");
    CORE_BYTE_SIZE_FORMAT_CASE(1ul << 10, "KiB");

    return std::format("{} byte{}", bytes, bytes == 1 ? "" : "s");
}
