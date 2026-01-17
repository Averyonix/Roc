#include "util.hpp"

std::string wrei_to_string(const wrei_stacktrace& st)
{
    std::string str;
    for (u32 i = 0; i < st.entries.size(); ++i) {
        if (!str.empty()) str += '\n';
        auto& e = st.entries[i];
        str += std::format("{:4}# {:4} at {}:{}", i, e.description(), e.source_file().c_str(), e.source_line());
    }
    return str;
}

std::pair<const wrei_stacktrace*, bool> wrei_stacktrace_cache::insert(const std::stacktrace& st)
{
    auto& entry = traces[st];
    bool added = entry.size() != st.size();
    if (added) {
        entry.populate(*this, st);
    }
    return {&entry, added};
}

void wrei_stacktrace::populate(struct wrei_stacktrace_cache& cache, const std::stacktrace& stacktrace)
{
    assert(entries.empty());

    entries.reserve(stacktrace.size());
    for (auto& e : stacktrace) {
        auto& cached = cache.entries[e];
        if (!cached.populated) {
            cached.description = e.description();
            cached.source_file = e.source_file();
            cached.source_line = e.source_line();
            cached.populated = true;
        }

        entries.emplace_back(&cached);
    }
}
