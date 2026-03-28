#include "memory.hpp"
#include "string.hpp"

std::string to_string(FmtBytes size)
{
    auto bytes = size.bytes;

#define BYTE_SIZE_FORMAT_CASE(Size, Suffix) \
    if (bytes >= (Size)) { \
        f64 in_size = f64(bytes) / (Size); \
        return format_with_suffix(Suffix, in_size, decimals_for_3sf(in_size)); \
    }

    BYTE_SIZE_FORMAT_CASE(1ul << 60, "EiB");
    BYTE_SIZE_FORMAT_CASE(1ul << 50, "PiB");
    BYTE_SIZE_FORMAT_CASE(1ul << 40, "TiB");
    BYTE_SIZE_FORMAT_CASE(1ul << 30, "GiB");
    BYTE_SIZE_FORMAT_CASE(1ul << 20, "MiB");
    BYTE_SIZE_FORMAT_CASE(1ul << 10, "KiB");

#undef BYTE_SIZE_FORMAT_CASE

    return std::format("{} byte{}", bytes, bytes == 1 ? "" : "s");
}
