#pragma once

#include <cstdint>
#include <optional>

#include "core/geometry/rect.h"

namespace harbor::island
{

// The window in front (its root owner), and where it is.
struct Foreground
{
    std::uint64_t window = 0;
    geometry::Rect bounds;
};

// A monitor's full-screen app, from Explorer's ABN_FULLSCREENAPP "open" until it ends (docs/adr/0014).
// Explorer's "closed" is unreliable, so the dock looks again on every foreground change, and every second
// while one is open (a browser leaving full screen moves no focus). Only the window that went full screen
// counts: with the taskbar on auto-hide, a maximized window covers the monitor too.
class FullScreenApp
{
  public:
    // Explorer: a full-screen app opened on monitor. foreground: the window in front then, if the dock may
    // judge it (test docks judge only their own test windows); it is the app if it covers the monitor.
    void Open(const geometry::Rect& monitor, const std::optional<Foreground>& foreground);
    // Explorer: it closed.
    void Close();
    // Looks again; false once it has ended: another window is in front, or the app no longer covers the
    // monitor. Before the app is known, the first window in front that covers the monitor is taken for it.
    // foreground nullopt: nothing the dock may judge is in front, so nothing changes.
    bool LookAgain(const geometry::Rect& monitor, const std::optional<Foreground>& foreground);

    bool IsOpen() const noexcept
    {
        return m_open;
    }

  private:
    bool m_open = false;
    std::optional<std::uint64_t> m_window;
};

} // namespace harbor::island
