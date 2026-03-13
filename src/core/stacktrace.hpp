#pragma once

#include "types.hpp"

struct core_stacktrace_entry_data
{
    bool populated = false;
    std::string description;
    std::filesystem::path source_file;
    u32 source_line;
};

struct core_stacktrace_entry
{
    const core_stacktrace_entry_data* data;

    const std::string& description() const noexcept { return data->description; }
    const std::filesystem::path& source_file() const noexcept { return data->source_file; }
    u32 source_line() const noexcept { return data->source_line; }
};

struct core_stacktrace
{
    std::vector<core_stacktrace_entry> entries;

    core_stacktrace() = default;

    void populate(struct core_stacktrace_cache& cache, const std::stacktrace& stacktrace);

    usz size() const noexcept { return entries.size(); }
    core_stacktrace_entry at(usz i) const { return entries.at(i); }

    auto begin() const noexcept { return entries.begin(); }
    auto end() const noexcept { return entries.end(); }
};

std::string core_to_string(const core_stacktrace& st);

struct core_stacktrace_cache
{
    ankerl::unordered_dense::segmented_map<std::stacktrace_entry, core_stacktrace_entry_data> entries;
    ankerl::unordered_dense::segmented_map<std::stacktrace, core_stacktrace> traces;

    std::pair<const core_stacktrace*, bool> insert(const std::stacktrace& st);
};
