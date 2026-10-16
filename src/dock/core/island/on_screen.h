#pragma once

#include <span>

#include "core/geometry/rect.h"

namespace harbor::island
{

// What the island's window looks like to the screen, read on the UI thread.
struct WindowFacts
{
    // Its latest frame is committed for the screen, and nothing was lost since.
    bool committed = false;
    bool visible = false;
    // DWM cloaked it: on another virtual desktop, or hidden by the shell or by us.
    bool cloaked = false;
    // Screen coordinates, physical pixels.
    geometry::Rect window;
};

// GuardPolicy's rule 7 (docs/adr/0007, 0011): the taskbar may be hidden only while this holds. Committed,
// visible, not cloaked, and entirely inside one of the connected monitors: an island straddling two monitors,
// or left where a monitor was, doesn't count.
bool IsOnScreen(const WindowFacts& facts, std::span<const geometry::Rect> monitors) noexcept;

} // namespace harbor::island
