#include "core/island/monitors.h"

#include <algorithm>
#include <utility>

namespace harbor::island
{

std::optional<std::size_t> DockMonitor(std::span<const MonitorInfo> monitors,
                                       std::optional<geometry::Rect> previous)
{
    if (monitors.empty())
    {
        return std::nullopt;
    }
    if (previous)
    {
        const geometry::Point centre{previous->left + previous->Width() / 2,
                                     previous->top + previous->Height() / 2};
        for (std::size_t i = 0; i < monitors.size(); ++i)
        {
            if (monitors[i].bounds.Contains(centre))
            {
                return i;
            }
        }
    }
    const auto primary = std::find_if(monitors.begin(), monitors.end(),
                                      [](const MonitorInfo& monitor) { return monitor.primary; });
    return primary != monitors.end() ? static_cast<std::size_t>(primary - monitors.begin()) : 0;
}

std::vector<geometry::Rect> EdgeTriggers(std::span<const MonitorInfo> monitors, std::size_t index,
                                         const geometry::Rect& strip, int raise)
{
    const auto& monitor = monitors[index];
    // The bottom edge's columns, minus those where another monitor continues below.
    std::vector<std::pair<int, int>> spans{
        {std::max(strip.left, monitor.bounds.left), std::min(strip.right, monitor.bounds.right)}};
    for (std::size_t i = 0; i < monitors.size(); ++i)
    {
        const auto& below = monitors[i].bounds;
        if (i == index || below.top != monitor.bounds.bottom)
        {
            continue;
        }
        std::vector<std::pair<int, int>> rest;
        for (const auto& [left, right] : spans)
        {
            if (below.left > left)
            {
                rest.emplace_back(left, std::min(right, below.left));
            }
            if (below.right < right)
            {
                rest.emplace_back(std::max(left, below.right), right);
            }
        }
        spans = std::move(rest);
    }

    const int bottom = strip.bottom - raise;
    std::vector<geometry::Rect> triggers;
    for (const auto& [left, right] : spans)
    {
        if (left < right && bottom > strip.top)
        {
            triggers.push_back({left, bottom - 1, right, bottom});
        }
    }
    return triggers;
}

bool RestsOnTrigger(const geometry::Rect& trigger, const MonitorInfo& monitor,
                    geometry::Point cursor) noexcept
{
    return cursor.x >= trigger.left && cursor.x < trigger.right && cursor.y >= trigger.top &&
           cursor.y < monitor.bounds.bottom;
}

bool TriggerAtEdge(const geometry::Rect& window, const MonitorInfo& monitor) noexcept
{
    return monitor.bounds.Contains(window) && window.bottom == monitor.bounds.bottom;
}

} // namespace harbor::island
