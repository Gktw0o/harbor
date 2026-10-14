#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace harbor::ipc
{

// Splits a pipe's byte stream into '\n'-terminated lines, the framing used by every Harbor pipe.
// A line longer than kMaxLineBytes is dropped up to and including its '\n', so one bad message can't
// grow memory without bound or desynchronise the stream; DroppedLineCount() lets callers log it.
class LineFramer
{
  public:
    static constexpr std::size_t kMaxLineBytes = 64 * 1024;

    // Appends bytes and returns every line they complete, without the '\n'.
    std::vector<std::string> Push(std::string_view bytes);

    std::size_t DroppedLineCount() const noexcept
    {
        return m_droppedLines;
    }

  private:
    std::string m_pending;
    bool m_discarding = false;
    std::size_t m_droppedLines = 0;
};

} // namespace harbor::ipc
