#include "core/island/click_policy.h"

#include <algorithm>
#include <iterator>
#include <vector>

namespace harbor::island
{

ClickAction DecideClick(std::span<const windows::AppWindows::Window> windows,
                        std::optional<windows::WindowId> foreground)
{
    using Window = windows::AppWindows::Window;
    if (windows.empty())
    {
        return {.kind = ClickAction::Kind::Launch};
    }
    std::vector<Window> bySeen(windows.begin(), windows.end());
    std::sort(bySeen.begin(), bySeen.end(), [](const Window& a, const Window& b) { return a.seen < b.seen; });

    const auto front = std::find_if(bySeen.begin(), bySeen.end(), [&](const Window& window) {
        return foreground && window.id == *foreground;
    });
    if (front == bySeen.end())
    {
        // Most recently in front; never-activated windows by the order seen, the newest first.
        const auto recent =
            std::max_element(bySeen.begin(), bySeen.end(), [](const Window& a, const Window& b) {
                return a.active != b.active ? a.active < b.active : a.seen < b.seen;
            });
        return {.kind = ClickAction::Kind::Activate, .window = recent->id, .restore = recent->minimized};
    }
    if (bySeen.size() == 1)
    {
        return {.kind = ClickAction::Kind::Minimize, .window = front->id};
    }
    const auto next = std::next(front) != bySeen.end() ? std::next(front) : bySeen.begin();
    return {.kind = ClickAction::Kind::Activate, .window = next->id, .restore = next->minimized};
}

} // namespace harbor::island
