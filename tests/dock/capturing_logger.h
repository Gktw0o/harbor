#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "ports/logger.h"

namespace harbor::test
{

// ILogger that keeps every message for assertions.
class CapturingLogger final : public log::ILogger
{
  public:
    void Write(log::Level level, std::string_view message) noexcept override
    try
    {
        std::lock_guard lock(m_mutex);
        m_lines.push_back(std::string(log::ToString(level)) + ": " + std::string(message));
    }
    catch (...)
    {
    }

    std::vector<std::string> Lines() const
    {
        std::lock_guard lock(m_mutex);
        return m_lines;
    }

    bool Contains(std::string_view text) const
    {
        for (const auto& line : Lines())
        {
            if (line.find(text) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

  private:
    mutable std::mutex m_mutex;
    std::vector<std::string> m_lines;
};

} // namespace harbor::test
