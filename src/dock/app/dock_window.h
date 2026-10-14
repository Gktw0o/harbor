#pragma once

#include <windows.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "adapters/composition_renderer.h"
#include "app/latest_value.h"
#include "app/taskbar_guard.h"
#include "core/island/appearance.h"
#include "core/island/layout.h"
#include "ports/ipc_channel.h"
#include "ports/logger.h"

namespace harbor::dock
{

// The island: a small topmost popup at the bottom of the primary monitor that never takes focus, drawn by
// CompositionRenderer. It follows the monitor's DPI, the work area and the Windows appearance settings; it
// is shown only once its first frame is committed. Until the AppBar arrives it sits at the bottom of the work
// area. The taskbar itself is the TaskbarGuard's business. Per-monitor-v2 DPI awareness comes from the
// application manifest.
//
// Owns the control channel's lifetime: starts it once the window exists and stops it when the window is
// destroyed. Pings are answered from this window's (UI) thread, so a pong also proves the UI thread is
// responsive. Everything that affects the taskbar (connection, pings, setTaskbar) goes straight from the
// channel's thread to the guard, never through this thread. dockItems comes here.
class DockWindow
{
  public:
    struct Options
    {
        // Test docks: close the window (and so exit) after this long without a control connection, so a test
        // that dies never leaves a dock behind. Zero: never.
        std::chrono::milliseconds orphanTimeout{0};
    };

    enum class CreateResult
    {
        Created,
        // GetLastError tells why.
        WindowFailed,
        // Composition can't draw into the window.
        GraphicsFailed,
    };

    // All four must outlive the window. The renderer must be initialized.
    DockWindow(ipc::IIpcChannel& controlChannel, TaskbarGuard& guard, adapters::CompositionRenderer& renderer,
               log::ILogger& log, Options options = {});
    ~DockWindow();
    DockWindow(const DockWindow&) = delete;
    DockWindow& operator=(const DockWindow&) = delete;

    CreateResult Create(HINSTANCE instance);

    HWND Hwnd() const noexcept
    {
        return m_hwnd;
    }

    // The window class, for tests that look for the island.
    static constexpr wchar_t kWindowClass[] = L"Harbor.Dock.Island";

    // Test docks only: posting this makes the UI thread hang for good, so tests can check that Harbor Shell
    // notices a dock whose UI stopped answering while its other threads still run.
    static constexpr UINT kTestHangMessage = WM_APP + 3;

  private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void StartControlChannel();
    void OnConnectionChanged(bool connected);
    void OnDockItems();
    // Reads the monitor, work area and appearance again and redraws if anything changed (or if forced).
    void Refresh(bool force);
    void OnCommitted();
    void OnMouseMove(LPARAM position);
    void OnClick(LPARAM position);
    void SetHovered(std::optional<std::size_t> hovered);
    std::optional<std::size_t> ItemAt(LPARAM position) const;
    void UpdatePresentable();

    ipc::IIpcChannel& m_controlChannel;
    TaskbarGuard& m_guard;
    adapters::CompositionRenderer& m_renderer;
    log::ILogger& m_log;
    Options m_options;
    // From the channel's thread to this one.
    LatestValue<ipc::DockItems> m_receivedItems;
    // What the dock shows. Kept while the Shell is away.
    ipc::DockItems m_items;
    HWND m_hwnd = nullptr;
    HPOWERNOTIFY m_powerNotification = nullptr;
    UINT m_taskbarCreatedMessage = 0;
    bool m_connected = false;

    // What is on screen.
    std::optional<island::Layout> m_layout;
    island::IslandStyle m_style;
    unsigned m_dpi = 0;
    std::optional<std::size_t> m_hovered;
    bool m_trackingMouse = false;
    // A frame has been committed since the window was created.
    bool m_committed = false;
    bool m_presentable = false;
};

} // namespace harbor::dock
