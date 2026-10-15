#pragma once

#include <chrono>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/windows/app_windows.h"
#include "ports/window_system.h"

namespace harbor::dock
{

// The window watcher's logic without its thread (docs/adr/0010): reads windows from an IWindowSystem when
// events say they changed and keeps AppWindows up to date. A window's app is read again a moment after it
// first appears (kRecheckDelays), and whenever it comes to the front, since some apps set their
// AppUserModelID late.
class WindowTracker
{
  public:
    using Milliseconds = std::chrono::milliseconds;
    static constexpr Milliseconds kRecheckDelays[] = {Milliseconds(500), Milliseconds(2000)};

    // classPrefix: test docks follow only windows whose class starts with it; empty: every window.
    WindowTracker(windows::IWindowSystem& system, std::string classPrefix);

    // Looks at every top-level window: at start, after Explorer restarted, or after events were lost. The
    // z-order seeds which window was in front last.
    void Rescan(Milliseconds now);
    void OnEvents(const std::vector<windows::WindowEvent>& events, Milliseconds now);
    // Reads again the windows whose second look is due.
    void OnTimer(Milliseconds now);
    // When OnTimer is due next.
    std::optional<Milliseconds> NextTimer() const;

    const windows::AppWindows& Apps() const noexcept
    {
        return m_apps;
    }
    // A window is being moved or sized (no edge triggers then: docs/adr/0011).
    bool MovingOrSizing() const noexcept
    {
        return !m_moving.empty();
    }

  private:
    // Reads the window's facts and app again: adds, updates, moves or removes it.
    void Consider(windows::WindowId window, Milliseconds now);

    windows::IWindowSystem& m_system;
    std::string m_classPrefix;
    windows::AppWindows m_apps;
    // (when, which window) second looks, in no order.
    std::vector<std::pair<Milliseconds, windows::WindowId>> m_rechecks;
    std::set<windows::WindowId> m_moving;
};

} // namespace harbor::dock
