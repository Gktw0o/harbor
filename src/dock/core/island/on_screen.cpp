#include "core/island/on_screen.h"

#include <algorithm>

namespace harbor::island
{

bool IsOnScreen(const WindowFacts& facts, std::span<const geometry::Rect> monitors) noexcept
{
    return facts.committed && facts.visible && !facts.cloaked && (!facts.tucked || facts.triggerArmed) &&
           std::any_of(monitors.begin(), monitors.end(),
                       [&](const geometry::Rect& monitor) { return monitor.Contains(facts.window); });
}

} // namespace harbor::island
