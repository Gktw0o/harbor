#pragma once

#include <string_view>

#include "core/log/log_line.h"

namespace harbor::log
{

// Port: where the dock's diagnostics go. Messages are UTF-8. Thread-safe; never throws.
class ILogger
{
  public:
    virtual ~ILogger() = default;

    virtual void Write(Level level, std::string_view message) noexcept = 0;

    void Info(std::string_view message) noexcept
    {
        Write(Level::Info, message);
    }

    void Warn(std::string_view message) noexcept
    {
        Write(Level::Warning, message);
    }

    void Error(std::string_view message) noexcept
    {
        Write(Level::Error, message);
    }
};

} // namespace harbor::log
