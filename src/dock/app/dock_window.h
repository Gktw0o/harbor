#pragma once

#include <windows.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include <wil/resource.h>

#include "adapters/composition_renderer.h"
#include "adapters/uia_island.h"
#include "app/appbar_worker.h"
#include "app/launcher.h"
#include "app/message_queue.h"
#include "app/taskbar_guard.h"
#include "app/window_watcher.h"
#include "core/island/appearance.h"
#include "core/island/dock_model.h"
#include "core/island/layout.h"
#include "core/island/monitors.h"
#include "ports/ipc_channel.h"
#include "ports/logger.h"

namespace harbor::dock
{

// The island: a small topmost popup at the bottom of the dock's monitor that never takes focus, drawn by
// CompositionRenderer. It follows the monitor's DPI, the work area and the Windows appearance settings; it
// is shown only once its first frame is committed. Until the AppBar arrives it sits at the bottom of the work
// area. The taskbar itself is the TaskbarGuard's business. Per-monitor-v2 DPI awareness comes from the
// application manifest.
//
// Owns the control channel's lifetime: starts it once the window exists and stops it when the window is
// destroyed. Pings are answered from this window's (UI) thread, so a pong also proves the UI thread is
// responsive. Everything that affects the taskbar (connection, pings, setTaskbar) goes straight from the
// channel's thread to the guard, never through this thread. dockItems comes here.
class DockWindow : private adapters::IIslandAccess
{
  public:
    struct Options
    {
        // Test docks: close the window (and so exit) after this long without a control connection, so a test
        // that dies never leaves a dock behind. Zero: never.
        std::chrono::milliseconds orphanTimeout{0};
        // Where the Shell leaves the items' icon files (<data dir>\icons).
        std::filesystem::path iconsDir;
        // Test docks: an AppBar that reserves nothing (adapters::TestAppBar).
        bool testAppBar = false;
        // Test docks: follow only windows whose class starts with this (WindowTracker); empty: every window.
        std::string windowClassPrefix;
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

    // After the message loop ended (and the guard put the taskbar back): removes the AppBar, ends the threads
    // and destroys the window. Safe to call more than once.
    void Shutdown() noexcept;

    HWND Hwnd() const noexcept
    {
        return m_hwnd;
    }

    // The window class, for tests that look for the island.
    static constexpr wchar_t kWindowClass[] = L"Harbor.Dock.Island";

    // Test docks only: posting this makes the UI thread hang for good, so tests can check that Harbor Shell
    // notices a dock whose UI stopped answering while its other threads still run.
    static constexpr UINT kTestHangMessage = WM_APP + 3;

    // The renderer posts this when the graphics device is lost; test docks post it to rehearse that. The
    // island goes off screen (so the taskbar comes back), gets a new device and is drawn again.
    static constexpr UINT kDeviceLostMessage = WM_APP + 6;

    // Posted when the island is cloaked or uncloaked: is it still on screen?
    static constexpr UINT kCloakChangedMessage = WM_APP + 10;

    // Test docks only: cloaks (wParam 1) or uncloaks (0) the island, as a switch to another virtual desktop
    // would, so tests can check a cloaked island brings the taskbar back.
    static constexpr UINT kTestCloakMessage = WM_APP + 11;

    // Test docks only: opens the right-click menu of item wParam, or closes an open menu, so tests can reach
    // it without moving the user's mouse.
    static constexpr UINT kTestOpenMenuMessage = WM_APP + 13;
    static constexpr UINT kTestCloseMenuMessage = WM_APP + 14;
    // Test docks only: shows the name label for item wParam (as resting the mouse on it would), or hides it.
    static constexpr UINT kTestShowLabelMessage = WM_APP + 15;
    static constexpr UINT kTestHideLabelMessage = WM_APP + 16;

  private:
    struct MonitorHost;
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void StartControlChannel();
    void OnConnectionChanged(bool connected);
    // Applies the dockItems and appInfo that came, in order.
    void OnControlMessages();
    // Asks Harbor Shell about running apps it hasn't been asked about on this connection, at most
    // kMaxLookupsInFlight unanswered at a time.
    void SendLookups();
    // The window watcher has a new snapshot.
    void OnWindowsChanged();
    // Builds the dock model again from the pinned items and the running apps; redraws if the items changed
    // (always if forced), or only shows the new marks.
    void RebuildModel(bool force);
    // The marks of the visible items.
    std::vector<island::ItemMarks> VisibleMarks() const;
    // The windows of every app an item stands for.
    std::vector<windows::AppWindows::Window> WindowsOf(const island::DockSlot& slot) const;
    // Reads the monitor, work area and appearance again and redraws if anything changed (or if forced).
    void Refresh(bool force);
    void OnCommitted(WPARAM generation);
    void OnDeviceLost();
    void OnMouseMove(LPARAM position);
    void OnClick(LPARAM position);
    // The monitors' hosts (MonitorHost), made or let go to match the connected monitors.
    static LRESULT CALLBACK HostProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    void SyncHosts();
    void StopHost(MonitorHost& host) noexcept;
    // Which host's monitor the island is on.
    std::size_t DockHost() const;
    // A host's granted strip, or until there is one, the bottom height pixels of its work area.
    geometry::Rect StripOf(const MonitorHost& host, int height) const;
    // Shows each other monitor's edge trigger (or hides it: the dock's own monitor, a full-screen app,
    // panic).
    void PlaceTriggers();
    void OnHostMouseMove(std::size_t index);
    void OnEdgeTimer();
    // The island goes to host index's monitor, in one move (docs/adr/0011).
    void MoveDock(std::size_t index);
    void OnAppBarNotification(std::size_t index, WPARAM notification, LPARAM lParam);
    // Ends the message loop with exitCode; the window stays until Shutdown.
    void RequestExit(int exitCode);
    // Takes ownership of report.
    void OnLaunched(LaunchReport* report);
    void SetHovered(std::optional<std::size_t> hovered);
    std::optional<std::size_t> ItemAtClient(LPARAM position) const;
    // A click or UI Automation's Invoke (docs/adr/0010): brings a window forward, minimizes one, or starts
    // the item; the "…" button opens the overflow menu. how is for the log.
    bool ClickItem(std::size_t index, std::string_view how);
    // The same for dock item index, on the island or behind the "…" button.
    bool ClickSlot(std::size_t index, std::string_view how);
    // The "…" button's menu of the items that didn't fit, with their icons.
    void OpenOverflowMenu(std::string_view how);
    // Starts a pinned item. False for anything else.
    bool LaunchItem(std::size_t index, std::string_view how);
    // Brings another app's window to the front (restoring it first), without waiting on the app.
    void BringForward(windows::WindowId window, bool restore);
    void OnRightClick(LPARAM position);
    // The name label above item index (docs/adr/0010), or none.
    void ShowLabel(std::size_t index);
    void HideLabel();
    // The item's right-click menu (docs/adr/0010): blocks in the menu's loop, then carries out the choice.
    void OpenMenu(std::size_t index, std::string_view how);
    // Tells the guard whether the island is on screen (rule 7). recheck: the second look after a display
    // change.
    void UpdatePresentable(bool recheck = false);

    // Gives every item in m_items its UI Automation key: the one it had in the previous list, if it was
    // there.
    void AssignItemKeys();

    // adapters::IIslandAccess, for UI Automation: the visible items.
    std::size_t ItemCount() const override;
    std::uint64_t ItemKey(std::size_t index) const override;
    std::optional<std::size_t> IndexOfKey(std::uint64_t key) const override;
    std::wstring ItemName(std::size_t index) const override;
    std::wstring ItemStatus(std::size_t index) const override;
    bool IsOverflow(std::size_t index) const override;
    bool IsOverflowMenuOpen() const override;
    void CloseMenu() override;
    RECT ItemBounds(std::size_t index) const override;
    std::optional<std::size_t> ItemAt(POINT screenPoint) const override;
    bool InvokeItem(std::size_t index) override;

    ipc::IIpcChannel& m_controlChannel;
    TaskbarGuard& m_guard;
    adapters::CompositionRenderer& m_renderer;
    log::ILogger& m_log;
    Options m_options;
    // From the channel's thread to this one: dockItems and appInfo, in order.
    MessageQueue<ipc::Message> m_received{4096};
    // What Harbor Shell said about each running app, by app id; kept across reconnects until it answers
    // again.
    std::map<windows::AppId, ipc::AppInfo> m_appInfo;
    // Apps asked about on this connection, and those not answered yet.
    static constexpr std::size_t kMaxLookupsInFlight = 64;
    std::set<windows::AppId> m_lookedUp;
    std::set<windows::AppId> m_lookupsInFlight;
    // The pinned items. Kept while the Shell is away.
    ipc::DockItems m_items;
    // What the dock shows: the pinned items, then the running apps that aren't.
    island::DockModel m_model;
    // When the dock first saw each running app: its wait for Harbor Shell's answer starts then.
    std::map<windows::AppId, std::chrono::milliseconds> m_appsSeen;
    // The running apps asking for attention now (to log each once).
    std::set<windows::AppId> m_attention;
    // Each item's UI Automation key, and the keys by item identity (a pin's kind, target, and how many times
    // the same pin came before it; a running app's id) for the next list.
    std::vector<std::uint64_t> m_itemKeys;
    std::map<std::string, std::uint64_t> m_keysByIdentity;
    std::uint64_t m_nextItemKey = 1;
    HWND m_hwnd = nullptr;
    // One per connected monitor (docs/adr/0011): a never-activated window that holds the monitor's AppBar
    // strip (each with its own worker thread) and is its edge trigger.
    struct MonitorHost
    {
        island::MonitorInfo monitor;
        HWND window = nullptr;
        std::unique_ptr<AppBarWorker> appBar;
        // The strip Explorer granted; until then, the bottom of the work area.
        std::optional<geometry::Rect> strip;
        // A full-screen app covers this monitor.
        bool fullScreen = false;
    };
    std::vector<MonitorHost> m_hosts;
    HINSTANCE m_instance = nullptr;
    // The bounds of the monitor the island is on; nullopt: the primary.
    std::optional<geometry::Rect> m_dockMonitor;
    // The edge triggers answer (off after the panic hotkey, until hiding is turned on again).
    bool m_triggersOn = true;
    // The host whose trigger the cursor reached, while the rest before moving runs.
    std::optional<std::size_t> m_triggerHost;
    std::optional<Launcher> m_launcher;
    // Follows the app windows on its own thread; its newest snapshot is m_windows.
    std::unique_ptr<WindowWatcher> m_watcher;
    WindowWatcher::Snapshot m_windows;
    std::unique_ptr<adapters::IslandAccessibility> m_accessibility;
    HPOWERNOTIFY m_powerNotification = nullptr;
    // This thread's windows being cloaked or uncloaked.
    wil::unique_hwineventhook m_cloakHook;
    UINT m_taskbarCreatedMessage = 0;
    bool m_connected = false;

    // What is on screen.
    std::optional<island::Layout> m_layout;
    island::IslandStyle m_style;
    unsigned m_dpi = 0;
    std::optional<std::size_t> m_hovered;
    bool m_trackingMouse = false;
    bool m_highContrast = false;
    // A menu's loop is running (one at a time), and whether it is the overflow menu.
    bool m_menuOpen = false;
    bool m_overflowMenuOpen = false;
    // The "…" button's place, when items don't fit (the items from there on are in its menu), and how many
    // items there were when that was logged.
    std::optional<std::size_t> m_overflow;
    std::size_t m_overflowCount = 0;
    // The name label's window (null if composition can't draw it), and the item it shows, if shown.
    HWND m_label = nullptr;
    std::optional<std::size_t> m_labelItem;
    // Each Render's number; only the latest one's commit counts.
    WPARAM m_renderGeneration = 0;
    // The latest frame is committed (and nothing was lost since).
    bool m_committed = false;
    // Shown once, after the first frame; after that only the panic hotkey hides it.
    bool m_shown = false;
    bool m_presentable = false;
    bool m_deviceLost = false;
    int m_deviceFailures = 0;
    // What the process exits with once the window is gone.
    int m_exitCode = 0;
};

} // namespace harbor::dock
