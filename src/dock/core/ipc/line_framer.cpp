#include "core/ipc/line_framer.h"

namespace harbor::ipc
{

std::vector<std::string> LineFramer::Push(std::string_view bytes)
{
    std::vector<std::string> lines;
    while (!bytes.empty())
    {
        const auto newline = bytes.find('\n');
        const auto chunk = bytes.substr(0, newline);

        if (!m_discarding)
        {
            if (m_pending.size() + chunk.size() > kMaxLineBytes)
            {
                m_discarding = true;
                m_pending.clear();
            }
            else
            {
                m_pending.append(chunk);
            }
        }

        if (newline == std::string_view::npos)
        {
            break;
        }

        if (m_discarding)
        {
            m_discarding = false;
            ++m_droppedLines;
        }
        else
        {
            lines.push_back(std::move(m_pending));
            m_pending.clear();
        }
        bytes.remove_prefix(newline + 1);
    }
    return lines;
}

} // namespace harbor::ipc
