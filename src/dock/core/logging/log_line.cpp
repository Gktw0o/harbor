#include "core/logging/log_line.h"

#include <format>

namespace harbor::log
{

const char* ToString(Level level) noexcept
{
    switch (level)
    {
    case Level::Info:
        return "info";
    case Level::Warning:
        return "warn";
    case Level::Error:
        return "error";
    }
    return "unknown";
}

std::string FormatLine(const Timestamp& time, Level level, std::string_view message)
{
    std::string line =
        std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z [{}] ", time.year, time.month, time.day,
                    time.hour, time.minute, time.second, time.millisecond, ToString(level));
    line.reserve(line.size() + message.size() + 1);
    for (const char c : message)
    {
        if (c == '\r')
        {
            line += "\\r";
        }
        else if (c == '\n')
        {
            line += "\\n";
        }
        else
        {
            line.push_back(c);
        }
    }
    line.push_back('\n');
    return line;
}

} // namespace harbor::log
