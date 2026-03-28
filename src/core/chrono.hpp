#pragma once

#include "types.hpp"

inline
std::chrono::system_clock::time_point time_current()
{
    return std::chrono::system_clock::now();
}

// Assume steady_clock is implemented as CLOCK_MONOTONIC.
static constexpr int steady_clock_id = CLOCK_MONOTONIC;

template<int ClockID>
std::chrono::steady_clock::time_point steady_clock_from_timespec(const timespec& ts)
{
    static_assert(steady_clock_id == ClockID);

    auto ns = ts.tv_sec * 1'000'000'000 + ts.tv_nsec;
    auto dur = std::chrono::nanoseconds(ns);
    return std::chrono::steady_clock::time_point(dur);
}

template<int ClockID>
timespec steady_clock_to_timespec(std::chrono::steady_clock::time_point tp)
{
    static_assert(steady_clock_id == ClockID);

    auto ns = tp.time_since_epoch().count();

    timespec ts;
    ts.tv_sec  = ns / 1'000'000'000;
    ts.tv_nsec = ns % 1'000'000'000;
    return ts;
}

enum class TimeFormat : u32
{
    iso8601,
    date_pretty,
    datetime,
    datetime_ms,
    time,
    time_ms,
};

std::string to_string(std::chrono::system_clock::time_point, TimeFormat);

std::string to_string(std::chrono::duration<f64, std::nano> dur);
