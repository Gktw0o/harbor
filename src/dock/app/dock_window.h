#pragma once

#include <windows.h>

#include <cstdint>

#include "ports/ipc_channel.h"

namespace harbor::dock
{

// M0 placeholder: a normal top-level window. It is not an AppBar and does not touch the taskbar.
// Per-monitor-v2 DPI awareness comes from the application manifest.
//
// Owns the control channel's lifetime: starts it once the window exists and stops it when the window is
// destroyed. Pings are answered from this window's (UI) thread, so a pong also proves the UI thread is
// responsive.
class DockWindow
{
  public:
    explicit DockWindow(ipc::IIpcChannel& controlChannel);
    DockWindow(const DockWindow&) = delete;
    DockWindow& operator=(const DockWindow&) = delete;

    // Returns false (with GetLastError set) if the window class or window can't be created.
    bool Create(HINSTANCE instance, int showCommand);

  private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void StartControlChannel();
    void OnPing(std::uint32_t seq);
    void UpdateTitle();

    ipc::IIpcChannel& m_controlChannel;
    HWND m_hwnd = nullptr;
    bool m_connected = false;
    bool m_hasPing = false;
    std::uint32_t m_lastPingSeq = 0;
};

} // namespace harbor::dock
