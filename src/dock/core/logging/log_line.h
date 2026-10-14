#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace harbor::log
{

enum class Level
{
    Info,
    Warning,
    Error,
};

// UTC time of a log entry, to the millisecond.
struct Timestamp
{
    std::uint16_t year = 0;
    std::uint16_t month = 0;
    std::uint16_t day = 0;
    std::uint16_t hour = 0;
    std::uint16_t minute = 0;
    std::uint16_t second = 0;
    std::uint16_t millisecond = 0;
};

// "info", "warn" or "error".
const char* ToString(Level level) noexcept;

// One log entry as a line: "2026-10-14T15:30:00.123Z [info] message\n". CR and LF inside the message are
// written as "\r" and "\n" so each entry stays on one line. Must match Harbor.Core's FileLog; both sides
// test the same cases.
std::string FormatLine(const Timestamp& time, Level level, std::string_view message);

} // namespace harbor::log
