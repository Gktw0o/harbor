#pragma once

#include <string>

#include "core/geometry/rect.h"

namespace harbor::windows
{

// Who cloaked a window (DWMWA_CLOAKED).
enum class Cloak
{
    None,
    // The app itself: it is still its app's window, just hidden for a moment.
    App,
    // The shell: on another virtual desktop, or a packaged app's frame that isn't showing.
    Shell,
    // Its owner is cloaked.
    Inherited,
};

// What the window watcher reads about a top-level window to decide whether it is an app window.
struct WindowFacts
{
    bool visible = false;
    // GetWindow(GW_OWNER) is set.
    bool owned = false;
    // WS_EX_TOOLWINDOW, WS_EX_APPWINDOW, WS_EX_NOACTIVATE.
    bool toolWindow = false;
    bool appWindow = false;
    bool noActivate = false;
    Cloak cloak = Cloak::None;
    // IVirtualDesktopManager says it is on another virtual desktop.
    bool onOtherDesktop = false;
    // Screen coordinates.
    geometry::Rect rect;
    // Its window class (ASCII; other names never match), and whose it is.
    std::string className;
    bool explorer = false;
    bool harbor = false;
};

// An app window, as Alt+Tab and the taskbar count them (docs/adr/0010): visible, with an area; not a tool
// window, unless it asks for a taskbar button (WS_EX_APPWINDOW), which also lets an owned window or one that
// never activates in; not cloaked by the shell or on another virtual desktop; and none of the desktop's,
// Explorer's taskbar windows, a hung window's ghost, or Harbor's own.
bool IsAppWindow(const WindowFacts& facts) noexcept;

} // namespace harbor::windows
