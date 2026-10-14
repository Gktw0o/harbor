#pragma once

#include <windows.h>

namespace harbor::dock
{

// M0 placeholder: a normal top-level window. It is not an AppBar and does not touch the taskbar.
// Per-monitor-v2 DPI awareness comes from the application manifest.
class DockWindow
{
  public:
    DockWindow() = default;
    DockWindow(const DockWindow&) = delete;
    DockWindow& operator=(const DockWindow&) = delete;

    // Returns false (with GetLastError set) if the window class or window can't be created.
    bool Create(HINSTANCE instance, int showCommand);

  private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    HWND m_hwnd = nullptr;
};

} // namespace harbor::dock
