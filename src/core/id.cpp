#include "id.hpp"

auto uid_allocate() -> Uid
{
    static std::atomic<u64> prev = {};
    return Uid(++prev);
}
