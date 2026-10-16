#include "core/windows/eligibility.h"

#include <array>
#include <string_view>

namespace harbor::windows
{
namespace
{

// Explorer's own windows that are never app windows: the desktop, the taskbars, and helpers that can look
// like one (the first-sign-in screen's proxy).
constexpr std::array<std::string_view, 6> kExplorerShellClasses{
    "Progman", "WorkerW", "Shell_TrayWnd", "Shell_SecondaryTrayWnd", "Shell_OOBEProxy", "Shell_CharmWindow",
};

bool IsExplorerShellWindow(const WindowFacts& facts) noexcept
{
    if (!facts.explorer)
    {
        return false;
    }
    for (const auto name : kExplorerShellClasses)
    {
        if (facts.className == name)
        {
            return true;
        }
    }
    return false;
}

} // namespace

bool IsAppWindow(const WindowFacts& facts) noexcept
{
    if (!facts.visible || facts.rect.Width() <= 0 || facts.rect.Height() <= 0)
    {
        return false;
    }
    if (facts.harbor || IsExplorerShellWindow(facts) || facts.className == "Ghost")
    {
        return false;
    }
    if (facts.cloak == Cloak::Shell || facts.cloak == Cloak::Inherited)
    {
        return false;
    }
    if (facts.appWindow)
    {
        return true;
    }
    return !facts.toolWindow && !facts.owned && !facts.noActivate;
}

} // namespace harbor::windows
