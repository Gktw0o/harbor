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
    // Behind a full-screen app on its monitor (docs/adr/0014): shown, but under that app.
    bool tucked = false;
    // Its monitor's edge trigger is in place and answering, so the mouse at the bottom brings it back.
    bool triggerArmed = false;
};

// GuardPolicy's rule 7 (docs/adr/0007, 0011, 0014): the taskbar may be hidden only while this holds.
// Committed, visible, not cloaked, and entirely inside one of the connected monitors: an island straddling
// two monitors, or left where a monitor was, doesn't count. Tucked behind a full-screen app, it counts only
// while its edge trigger is armed.
bool IsOnScreen(const WindowFacts& facts, std::span<const geometry::Rect> monitors) noexcept;

} // namespace harbor::island
