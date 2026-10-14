#pragma once

#include <windows.h>

#include <cstdint>

#include "app/taskbar_guard.h"
#include "ports/ipc_channel.h"
#include "ports/logger.h"

namespace harbor::dock
{

// M1 placeholder: a normal top-level window, not yet an AppBar. The taskbar itself is the TaskbarGuard's
// business. Per-monitor-v2 DPI awareness comes from the application manifest.
//
// Owns the control channel's lifetime: starts it once the window exists and stops it when the window is
// destroyed. Pings are answered from this window's (UI) thread, so a pong also proves the UI thread is
// responsive. Everything that affects the taskbar (connection, pings, setTaskbar) goes straight from the
// channel's thread to the guard, never through this thread.
class DockWindow
{
  public:
    // All three must outlive the window.
    DockWindow(ipc::IIpcChannel& controlChannel, TaskbarGuard& guard, log::ILogger& log);
    DockWindow(const DockWindow&) = delete;
    DockWindow& operator=(const DockWindow&) = delete;

    // Returns false (with GetLastError set) if the window class or window can't be created.
    bool Create(HINSTANCE instance, int showCommand);

    HWND Hwnd() const noexcept
    {
        return m_hwnd;
    }

    // Test docks only: posting this makes the UI thread hang for good, so tests can check that Harbor Shell
    // notices a dock whose UI stopped answering while its other threads still run.
    static constexpr UINT kTestHangMessage = WM_APP + 3;

  private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void StartControlChannel();
    void OnPing(std::uint32_t seq);
    void UpdateTitle();

    ipc::IIpcChannel& m_controlChannel;
    TaskbarGuard& m_guard;
    log::ILogger& m_log;
    HWND m_hwnd = nullptr;
    bool m_connected = false;
    bool m_hasPing = false;
    std::uint32_t m_lastPingSeq = 0;
    bool m_taskbarHidden = false;
};

} // namespace harbor::dock
