#pragma once

#include "types.hpp"

struct StacktraceEntryData
{
    bool populated = false;
    std::string description;
    std::filesystem::path source_file;
    u32 source_line;
};

struct StacktraceEntry
{
    const StacktraceEntryData* data;

    const std::string& description() const noexcept { return data->description; }
    const std::filesystem::path& source_file() const noexcept { return data->source_file; }
    u32 source_line() const noexcept { return data->source_line; }
};

struct Stacktrace
{
    std::vector<StacktraceEntry> entries;

    Stacktrace() = default;

    void populate(struct StacktraceCache& cache, const std::stacktrace& stacktrace);

    usz size() const noexcept { return entries.size(); }
    StacktraceEntry at(usz i) const { return entries.at(i); }

    auto begin() const noexcept { return entries.begin(); }
    auto end() const noexcept { return entries.end(); }
};

std::string to_string(const Stacktrace& st);

struct StacktraceCache
{
    ankerl::unordered_dense::segmented_map<std::stacktrace_entry, StacktraceEntryData> entries;
    ankerl::unordered_dense::segmented_map<std::stacktrace, Stacktrace> traces;

    std::pair<const Stacktrace*, bool> insert(const std::stacktrace& st);
};
