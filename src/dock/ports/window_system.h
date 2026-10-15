#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/windows/app_windows.h"
#include "core/windows/eligibility.h"

namespace harbor::windows
{

// A change to a top-level window, from the system's window events.
struct WindowEvent
{
    enum class Kind
    {
        // Shown, hidden, minimized or restored, cloaked or uncloaked: read its facts again.
        Changed,
        Destroyed,
        // It came to the front.
        Foreground,
        // The user started or stopped moving or sizing it.
        MoveSizeStart,
        MoveSizeEnd,
    };
    Kind kind = Kind::Changed;
    WindowId window = 0;
    friend bool operator==(const WindowEvent&, const WindowEvent&) = default;
};

// The app a window belongs to (docs/adr/0010).
struct WindowApp
{
    // UTF-8; empty: none, or unknown.
    std::string aumid;
    std::string exe;
    std::uint32_t processId = 0;
    // When the process started (FILETIME ticks).
    std::uint64_t processStarted = 0;
};

// Port: the desktop's top-level windows, as the window watcher sees them. Every call happens on the watcher's
// thread, which pumps messages.
class IWindowSystem
{
  public:
    virtual ~IWindowSystem() = default;

    // Starts collecting events about other processes' top-level windows. wake runs (on this thread, from
    // inside the message loop or a pumping call) when new events wait in TakeEvents; it may only post a
    // message.
    virtual bool Start(std::function<void()> wake) = 0;
    virtual void Stop() noexcept = 0;
    // The events since the last call, oldest first. overflowed: some were dropped, so look at every window
    // again.
    virtual std::vector<WindowEvent> TakeEvents(bool& overflowed) = 0;

    // Every top-level window, front to back.
    virtual std::vector<WindowId> TopLevelWindows() = 0;
    // nullopt once the window is gone.
    virtual std::optional<WindowFacts> FactsOf(WindowId window) = 0;
    virtual std::optional<WindowApp> AppOf(WindowId window) = 0;
    virtual bool IsMinimized(WindowId window) = 0;
    // The window at the root of window's owner chain (itself if unowned).
    virtual WindowId RootOwner(WindowId window) = 0;
};

} // namespace harbor::windows
