#include "app/dock_window.h"

#include <algorithm>
#include <dwmapi.h>
#include <format>
#include <iterator>
#include <limits>
#include <memory>
#include <shellapi.h>
#include <ShellScalingApi.h>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "adapters/icon_files.h"
#include "adapters/popup_menu.h"
#include "adapters/system_appearance.h"
#include "adapters/win32_app_bar.h"
#include "adapters/win32_window_system.h"
#include "app/exit_codes.h"
#include "core/ipc/codec.h"
#include "core/island/click_policy.h"
#include "core/island/icon_file.h"
#include "core/island/menu_model.h"
#include "core/island/on_screen.h"

namespace harbor::dock
{
namespace
{

constexpr DWORD kStyle = WS_POPUP;
// Never activated, no taskbar button, above normal windows, drawn only by composition.
constexpr DWORD kExStyle = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP;

// Posted by the control channel's thread; handled on the UI thread.
constexpr UINT kControlConnectionChanged = WM_APP + 1; // wParam: connected (0/1)
constexpr UINT kControlPing = WM_APP + 2;              // wParam: seq
constexpr UINT kControlMessages = WM_APP + 4;          // dockItems and appInfo wait in m_received
// Posted by the renderer once a frame is on its way to the screen.
constexpr UINT kRenderCommitted = WM_APP + 5;
// Posted to the island's window by a host when Explorer says AppBars may have to move: the work areas may
// have changed.
constexpr UINT kWorkAreaMayHaveChanged = WM_APP + 8;
// Posted by the AppBar worker to its host if Explorer granted a strip, which the dock never asks for.
constexpr UINT kAppBarGranted = WM_APP + 19;
// Explorer's AppBar notifications (ABN_*): wParam = notification.
constexpr UINT kAppBarCallback = WM_APP + 9;
// Posted by the window watcher: a new snapshot waits.
constexpr UINT kWindowsChanged = WM_APP + 12;
// Posted by UI Automation's Invoke or Expand on the "…" button: open the overflow menu outside that call.
constexpr UINT kOpenOverflowMessage = WM_APP + 17;
// The UI Automation keys of the "…", tray and Start buttons: no item has them.
constexpr std::uint64_t kOverflowKey = std::numeric_limits<std::uint64_t>::max();
constexpr std::uint64_t kTrayKey = kOverflowKey - 1;
constexpr std::uint64_t kStartKey = kOverflowKey - 2;

constexpr UINT_PTR kOrphanTimer = 1;
constexpr UINT_PTR kDeviceRetryTimer = 2;
// A running app waits for Harbor Shell's answer: build the model again when its wait ends.
constexpr UINT_PTR kModelTimer = 3;
// The name label: shown after resting on an item this long; its font and bitmap let go of after this long
// hidden.
constexpr UINT_PTR kLabelTimer = 4;
constexpr UINT kLabelDelayMs = 400;
constexpr UINT_PTR kLabelReleaseTimer = 5;
constexpr UINT kLabelReleaseMs = 60'000;
constexpr wchar_t kLabelClass[] = L"Harbor.Dock.Label";
// A monitor's host (its AppBar and edge trigger), and the trigger's rest before the dock moves there
// (docs/adr/0011).
constexpr wchar_t kHostClass[] = L"Harbor.Dock.Host";
constexpr UINT_PTR kEdgeTimer = 6;
constexpr UINT kEdgeRestMs = 400;
// Explorer's auto-hidden taskbar keeps a monitor's last 2 px: the trigger row sits above them.
constexpr int kTriggerRaise = 2;
// The tuck's own timer (island::Tuck::WakeAt), and the look at full-screen apps every second while one is
// open (docs/adr/0014): Explorer's "closed" is unreliable.
constexpr UINT_PTR kTuckTimer = 7;
constexpr UINT_PTR kFullScreenTimer = 8;
constexpr UINT kFullScreenLookMs = 1000;
// The Start button pressed the island's hotkey: if the island isn't open this much later, Windows' Start
// opens instead (docs/adr/0015).
constexpr UINT_PTR kIslandTimer = 9;
constexpr UINT kIslandAnswerMs = 500;
// A launch the island asked for, not one of the dock's items (Launcher's index).
constexpr std::size_t kIslandLaunch = std::numeric_limits<std::size_t>::max();
// Posted to look at the island's place again after a display change was handled (see UpdatePresentable).
constexpr UINT kRecheckPresentable = WM_APP + 18;
constexpr UINT kDeviceRetryMs = 2000;
// Failed attempts at a new graphics device before the dock gives up (and Harbor Shell restores the taskbar).
constexpr int kDeviceAttempts = 5;

geometry::Rect FromRect(const RECT& rect) noexcept
{
    return {rect.left, rect.top, rect.right, rect.bottom};
}

HWND ToHwnd(windows::WindowId id) noexcept
{
    return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(id));
}

windows::WindowId ToId(HWND hwnd) noexcept
{
    return static_cast<windows::WindowId>(reinterpret_cast<std::uintptr_t>(hwnd));
}

std::chrono::milliseconds SteadyNow() noexcept
{
    return std::chrono::milliseconds(GetTickCount64());
}

// A window's title as UTF-8. GetWindowText reads the title Windows keeps for another process's window rather
// than asking the window, so a hung app can't block it.
std::string TitleOf(HWND window)
{
    std::wstring title(512, L'\0');
    title.resize(static_cast<std::size_t>(std::max(GetWindowTextW(window, title.data(), 512), 0)));
    const int length = WideCharToMultiByte(CP_UTF8, 0, title.data(), static_cast<int>(title.size()), nullptr,
                                           0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(std::max(length, 0)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, title.data(), static_cast<int>(title.size()), utf8.data(), length,
                        nullptr, nullptr);
    return utf8;
}

const char* Describe(island::MenuCommand command) noexcept
{
    switch (command)
    {
    case island::MenuCommand::Title:
        return "nothing";
    case island::MenuCommand::Window:
        return "a window comes forward";
    case island::MenuCommand::Launch:
        return "start";
    case island::MenuCommand::Pin:
        return "pin";
    case island::MenuCommand::Unpin:
        return "unpin";
    case island::MenuCommand::CloseWindow:
        return "close its window";
    case island::MenuCommand::CloseAll:
        return "close all its windows";
    }
    return "?";
}

// The same items in the same order, looking the same: only their marks, or the apps behind them, may differ.
bool SameItems(const island::DockModel& a, const island::DockModel& b)
{
    if (a.pinned != b.pinned || a.slots.size() != b.slots.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < a.slots.size(); ++i)
    {
        const auto& x = a.slots[i];
        const auto& y = b.slots[i];
        if (x.kind != y.kind || x.pin != y.pin || x.name != y.name || x.icon != y.icon ||
            (x.kind == island::DockSlot::Kind::App && x.apps != y.apps))
        {
            return false;
        }
    }
    return true;
}

// Every connected monitor: bounds, work area, DPI, and whether it is the primary.
std::vector<island::MonitorInfo> ReadMonitors()
{
    std::vector<island::MonitorInfo> monitors;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM context) -> BOOL {
            try
            {
                MONITORINFO info{sizeof(info)};
                if (!GetMonitorInfoW(monitor, &info))
                {
                    return TRUE;
                }
                UINT dpiX = 0;
                UINT dpiY = 0;
                const unsigned dpi =
                    SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) && dpiX != 0
                        ? dpiX
                        : USER_DEFAULT_SCREEN_DPI;
                reinterpret_cast<std::vector<island::MonitorInfo>*>(context)->push_back(
                    {FromRect(info.rcMonitor), FromRect(info.rcWork), dpi,
                     (info.dwFlags & MONITORINFOF_PRIMARY) != 0});
                return TRUE;
            }
            catch (...)
            {
                return FALSE;
            }
        },
        reinterpret_cast<LPARAM>(&monitors));
    return monitors;
}

// Every connected monitor's rectangle.
std::vector<geometry::Rect> ConnectedMonitors()
{
    std::vector<geometry::Rect> monitors;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR, HDC, LPRECT rect, LPARAM context) -> BOOL {
            try
            {
                reinterpret_cast<std::vector<geometry::Rect>*>(context)->push_back(FromRect(*rect));
                return TRUE;
            }
            catch (...)
            {
                return FALSE;
            }
        },
        reinterpret_cast<LPARAM>(&monitors));
    return monitors;
}

// Out-of-context WinEvent hook for this thread's windows being cloaked or uncloaked (another virtual desktop,
// or the shell hiding them): no window message says so, and a cloaked island isn't on screen.
void CALLBACK OnCloakChanged(HWINEVENTHOOK, DWORD, HWND hwnd, LONG object, LONG child, DWORD, DWORD)
{
    wchar_t name[std::size(DockWindow::kWindowClass) + 1]{};
    if (object == OBJID_WINDOW && child == CHILDID_SELF &&
        GetClassNameW(hwnd, name, static_cast<int>(std::size(name))) != 0 &&
        std::wstring_view(name) == DockWindow::kWindowClass)
    {
        PostMessageW(hwnd, DockWindow::kCloakChangedMessage, 0, 0);
    }
}

} // namespace

DockWindow::DockWindow(ipc::IIpcChannel& controlChannel, TaskbarGuard& guard,
                       adapters::CompositionRenderer& renderer, log::ILogger& log, Options options)
    : m_controlChannel(controlChannel), m_guard(guard), m_renderer(renderer), m_log(log), m_options(options)
{
}

DockWindow::~DockWindow()
{
    Shutdown();
}

void DockWindow::RequestExit(int exitCode)
{
    // The window stays until Shutdown: the AppBar is removed while it still exists.
    m_exitCode = exitCode;
    PostQuitMessage(exitCode);
}

void DockWindow::Shutdown() noexcept
{
    // Its callbacks use this object.
    m_controlChannel.Stop();
    // The AppBars go before their windows do, waiting for Explorer a moment at most.
    for (auto& host : m_hosts)
    {
        StopHost(host);
    }
    m_hosts.clear();
    // A launch stuck in Explorer is left behind, and so is a watcher stuck in another app.
    m_launcher.reset();
    m_watcher.reset();
    if (m_hwnd != nullptr)
    {
        DestroyWindow(m_hwnd);
    }
}

DockWindow::CreateResult DockWindow::Create(HINSTANCE instance)
{
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &DockWindow::WndProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWindowClass;
    if (RegisterClassExW(&windowClass) == 0)
    {
        return CreateResult::WindowFailed;
    }

    // Placed and sized by the first Refresh; shown once its first frame is committed.
    if (CreateWindowExW(kExStyle, kWindowClass, L"Harbor Dock", kStyle, 0, 0, 0, 0, nullptr, nullptr,
                        instance, this) == nullptr)
    {
        return CreateResult::WindowFailed;
    }
    if (!m_renderer.Attach(m_hwnd, kDeviceLostMessage))
    {
        return CreateResult::GraphicsFailed;
    }
    // The name label: owned by the island (so above it), never activated, and clicks go through it.
    WNDCLASSEXW labelClass{};
    labelClass.cbSize = sizeof(labelClass);
    labelClass.lpfnWndProc = DefWindowProcW;
    labelClass.hInstance = instance;
    labelClass.lpszClassName = kLabelClass;
    RegisterClassExW(&labelClass);
    m_label = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_LAYERED |
                                  WS_EX_TRANSPARENT | WS_EX_NOREDIRECTIONBITMAP,
                              kLabelClass, L"", WS_POPUP, 0, 0, 0, 0, m_hwnd, nullptr, instance, nullptr);
    if (m_label != nullptr)
    {
        SetLayeredWindowAttributes(m_label, 0, 255, LWA_ALPHA);
        if (!m_renderer.AttachLabel(m_label))
        {
            DestroyWindow(m_label);
            m_label = nullptr;
        }
    }

    m_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
    // Energy saver turning on or off changes the island's look: the old battery saver flag, and Windows 11's
    // energy saver status, which turns the host backdrop black (docs/adr/0013). The second sends its value
    // right away.
    m_powerNotification =
        RegisterPowerSettingNotification(m_hwnd, &GUID_POWER_SAVING_STATUS, DEVICE_NOTIFY_WINDOW_HANDLE);
    m_energySaverNotification =
        RegisterPowerSettingNotification(m_hwnd, &GUID_ENERGY_SAVER_STATUS, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (m_energySaverNotification == nullptr)
    {
        m_log.Warn(std::format("island: no energy saver notifications ({})", GetLastError()));
    }
    // A cloaked island isn't on screen (docs/adr/0011). Only this thread's windows: the hook costs nothing
    // while nothing here is cloaked.
    m_cloakHook.reset(SetWinEventHook(EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED, nullptr, &OnCloakChanged,
                                      GetCurrentProcessId(), GetCurrentThreadId(), WINEVENT_OUTOFCONTEXT));
    if (!m_cloakHook)
    {
        m_log.Warn(std::format("island: no cloak notifications ({})", GetLastError()));
    }

    // A host per monitor: its AppBar hears of full-screen apps there, and its edge trigger moves the island
    // there (docs/adr/0011, 0014).
    WNDCLASSEXW hostClass{};
    hostClass.cbSize = sizeof(hostClass);
    hostClass.lpfnWndProc = &DockWindow::HostProc;
    hostClass.hInstance = instance;
    hostClass.lpszClassName = kHostClass;
    RegisterClassExW(&hostClass);
    m_instance = instance;
    SyncHosts();
    Refresh(/*force=*/true);
    const HWND hwnd = m_hwnd;
    m_launcher.emplace(m_hwnd);
    m_watcher = std::make_unique<WindowWatcher>(std::make_unique<adapters::Win32WindowSystem>(),
                                                m_options.windowClassPrefix, m_log,
                                                [hwnd] { PostMessageW(hwnd, kWindowsChanged, 0, 0); });
    // The base is private: converted here, where it's accessible.
    adapters::IIslandAccess& access = *this;
    m_accessibility = std::make_unique<adapters::IslandAccessibility>(m_hwnd, access);

    // The guard's state reports go out through this thread, like pongs.
    m_guard.SetStateWindow(m_hwnd);
    OnConnectionChanged(false);
    StartControlChannel();
    return CreateResult::Created;
}

void DockWindow::StartControlChannel()
{
    // Callbacks run on the channel's thread, so they only post (to this window and to the guard) and log,
    // all thread-safe. PostMessage to a window that has already been destroyed fails harmlessly.
    const HWND hwnd = m_hwnd;
    TaskbarGuard& guard = m_guard;
    log::ILogger& log = m_log;
    MessageQueue<ipc::Message>& received = m_received;
    m_controlChannel.Start({
        .onMessage =
            [hwnd, &guard, &log, &received](const ipc::Message& message) {
                if (const auto* ping = std::get_if<ipc::Ping>(&message))
                {
                    PostMessageW(hwnd, kControlPing, ping->seq, 0);
                    guard.PostPing();
                }
                else if (const auto* setTaskbar = std::get_if<ipc::SetTaskbar>(&message))
                {
                    guard.PostRequest(setTaskbar->hidden);
                }
                else if (std::holds_alternative<ipc::DockItems>(message) ||
                         std::holds_alternative<ipc::AppInfo>(message) ||
                         std::holds_alternative<ipc::IslandState>(message) ||
                         std::holds_alternative<ipc::Launch>(message))
                {
                    // In one queue, in order: an appInfo may name a pin from the dockItems before it.
                    switch (received.Push(message))
                    {
                    case MessageQueue<ipc::Message>::Pushed::First:
                        PostMessageW(hwnd, kControlMessages, 0, 0);
                        break;
                    case MessageQueue<ipc::Message>::Pushed::Queued:
                        break;
                    case MessageQueue<ipc::Message>::Pushed::Dropped:
                        log.Warn("control channel: the island isn't taking messages; dropped one");
                        break;
                    }
                }
            },
        .onConnectionChanged =
            [hwnd, &guard, &log](bool connected) {
                log.Info(connected ? "control channel: connected to Harbor Shell"
                                   : "control channel: disconnected");
                guard.PostLinkChanged(connected);
                PostMessageW(hwnd, kControlConnectionChanged, connected, 0);
            },
        .onDiagnostic =
            [&log](std::string_view text) {
                std::string line = "control channel: ";
                line.append(text);
                log.Warn(line);
            },
    });
}

void DockWindow::OnConnectionChanged(bool connected)
{
    m_connected = connected;
    if (m_options.orphanTimeout.count() > 0)
    {
        if (connected)
        {
            KillTimer(m_hwnd, kOrphanTimer);
        }
        else
        {
            // (Re)starts the countdown.
            SetTimer(m_hwnd, kOrphanTimer, static_cast<UINT>(m_options.orphanTimeout.count()), nullptr);
        }
    }
    // A new Harbor Shell (or the same one again) knows nothing of what this one asked: ask again. What the
    // last one answered stays until the new answers come.
    m_lookedUp.clear();
    m_lookupsInFlight.clear();
    SendLookups();
    // Nor where the dock is. Its island is gone with it until it says otherwise.
    m_sentDockState.reset();
    SendDockState();
    if (!connected)
    {
        OnIslandState({});
    }
}

void DockWindow::OnControlMessages()
{
    bool itemsChanged = false;
    bool appsChanged = false;
    for (auto& message : m_received.TakeAll())
    {
        if (auto* items = std::get_if<ipc::DockItems>(&message); items && *items != m_items)
        {
            m_items = std::move(*items);
            itemsChanged = true;
            // Only the count: the names are the user's business.
            m_log.Info(std::format("dock items: {} received", m_items.items.size()));
        }
        else if (auto* info = std::get_if<ipc::AppInfo>(&message))
        {
            m_lookupsInFlight.erase(info->id);
            auto& known = m_appInfo[info->id];
            appsChanged = appsChanged || known != *info;
            known = std::move(*info);
        }
        else if (const auto* islandState = std::get_if<ipc::IslandState>(&message))
        {
            OnIslandState(*islandState);
        }
        else if (const auto* launch = std::get_if<ipc::Launch>(&message))
        {
            OnIslandLaunch(launch->item);
        }
    }
    if (itemsChanged || appsChanged)
    {
        RebuildModel(/*force=*/itemsChanged);
    }
    // Answers make room for more questions.
    SendLookups();
}

void DockWindow::SendLookups()
{
    if (!m_connected)
    {
        return;
    }
    std::size_t sent = 0;
    for (const auto& app : m_windows.apps)
    {
        if (m_lookupsInFlight.size() >= kMaxLookupsInFlight)
        {
            break;
        }
        if (m_lookedUp.contains(app.id))
        {
            continue;
        }
        // What doesn't fit the message (or isn't known) goes as null; the Shell does with what it gets.
        ipc::AppLookup lookup{.id = app.id};
        if (ipc::IsWireText(app.aumid, ipc::AppLookup::kMaxAumidLength))
        {
            lookup.aumid = app.aumid;
        }
        if (ipc::IsWireText(app.exe, ipc::AppLookup::kMaxExeLength))
        {
            lookup.exe = app.exe;
        }
        if (!app.windows.empty() && app.windows.front().id <= std::numeric_limits<std::uint32_t>::max())
        {
            lookup.window = static_cast<std::uint32_t>(app.windows.front().id);
        }
        if (!m_controlChannel.Send(lookup))
        {
            break;
        }
        m_lookedUp.insert(app.id);
        m_lookupsInFlight.insert(app.id);
        ++sent;
    }
    if (sent > 0)
    {
        // How many, not which.
        m_log.Info(std::format("apps: asked Harbor Shell about {} running apps", sent));
    }
}

void DockWindow::OnWindowsChanged()
{
    if (m_watcher)
    {
        if (auto snapshot = m_watcher->TakeSnapshot())
        {
            m_windows = std::move(*snapshot);
            RebuildModel(/*force=*/false);
            SendLookups();
        }
    }
    // The foreground may have changed: a full-screen app may have ended without Explorer saying so.
    LookAgainAtFullScreen();
}

void DockWindow::RebuildModel(bool force)
{
    const auto now = SteadyNow();
    std::vector<island::RunningApp> running;
    std::map<windows::AppId, std::chrono::milliseconds> seen;
    std::set<windows::AppId> attending;
    for (const auto& app : m_windows.apps)
    {
        const auto known = m_appsSeen.find(app.id);
        const auto when = known != m_appsSeen.end() ? known->second : now;
        seen.emplace(app.id, when);
        const auto info = m_appInfo.find(app.id);
        const bool attention = std::any_of(app.windows.begin(), app.windows.end(),
                                           [](const auto& window) { return window.attention; });
        if (attention && !m_attention.contains(app.id))
        {
            // Which app id, never which app.
            m_log.Info(std::format("attention: app {} asks for attention", app.id));
        }
        if (attention)
        {
            attending.insert(app.id);
        }
        running.push_back({.id = app.id,
                           .identity = app.identity,
                           .fallbackName = island::FallbackName(app.aumid, app.exe),
                           .attention = attention,
                           .info = info != m_appInfo.end() ? std::optional(info->second) : std::nullopt,
                           .seen = when});
    }
    m_appsSeen = std::move(seen);
    m_attention = std::move(attending);

    auto model = island::BuildDockModel(m_items.items, running, now);
    KillTimer(m_hwnd, kModelTimer);
    if (model.rebuildAt)
    {
        const auto delay = std::max<long long>((*model.rebuildAt - now).count(), 1);
        SetTimer(m_hwnd, kModelTimer, static_cast<UINT>(delay), nullptr);
    }
    if (!force && model == m_model)
    {
        return;
    }
    const bool sameItems = !force && SameItems(model, m_model);
    m_model = std::move(model);
    AssignItemKeys();
    if (sameItems)
    {
        // Only the dots, the attention marks or the windows behind the items changed: nothing to redraw.
        m_renderer.SetMarks(VisibleMarks());
        return;
    }
    m_hovered.reset();
    HideLabel();
    Refresh(/*force=*/true);
    if (m_accessibility)
    {
        // Names and order may differ: UI Automation clients must look again.
        m_accessibility->ItemsChanged();
    }
}

std::vector<island::ItemMarks> DockWindow::VisibleMarks() const
{
    std::vector<island::ItemMarks> marks;
    const auto visible = m_layout ? std::min(m_layout->items.size(), m_places.entries.size()) : 0;
    for (std::size_t i = 0; i < visible; ++i)
    {
        const auto& place = m_places.entries[i];
        if (place.kind == island::SlotMap::Kind::Overflow && place.slot <= m_model.slots.size())
        {
            // The "…" button: highlighted while one of the apps behind it needs attention.
            const bool attention =
                std::any_of(m_model.slots.begin() + static_cast<std::ptrdiff_t>(place.slot),
                            m_model.slots.end(), [](const auto& slot) { return slot.marks.attention; });
            marks.push_back({.attention = attention});
            continue;
        }
        if (place.kind == island::SlotMap::Kind::Start)
        {
            // A dot while the island is open, as under a running app.
            marks.push_back({.running = m_island.open});
            continue;
        }
        if (place.kind != island::SlotMap::Kind::Slot || place.slot >= m_model.slots.size())
        {
            // The tray button.
            marks.push_back({});
            continue;
        }
        marks.push_back(m_model.slots[place.slot].marks);
    }
    return marks;
}

std::vector<windows::AppWindows::Window> DockWindow::WindowsOf(const island::DockSlot& slot) const
{
    std::vector<windows::AppWindows::Window> windows;
    for (const auto& app : m_windows.apps)
    {
        if (std::find(slot.apps.begin(), slot.apps.end(), app.id) != slot.apps.end())
        {
            windows.insert(windows.end(), app.windows.begin(), app.windows.end());
        }
    }
    return windows;
}

void DockWindow::Refresh(bool force)
{
    if (m_deviceLost)
    {
        // Nothing can be drawn now; getting a new device redraws everything.
        return;
    }
    if (m_hosts.empty())
    {
        return;
    }
    const auto& host = m_hosts[DockHost()];
    const auto& monitor = host.monitor;
    const auto appearance = adapters::ReadSystemAppearance(m_energySaver);
    const auto style = island::StyleFor(appearance);
    const auto slots = m_model.slots.size();
    // The Start button first (docs/adr/0015); a divider when running apps follow pinned items; the tray
    // button last while the taskbar is hidden.
    const bool divider = m_model.pinned > 0 && slots > m_model.pinned;
    const auto count = 1 + slots + (m_taskbarHidden ? 1 : 0);
    const auto metrics = island::MeasureFor(monitor.bounds.Width(), monitor.dpi, count, divider);
    const geometry::Rect strip = StripOf(host, metrics.StripHeight());
    auto layout = island::Arrange(strip, metrics, 1 + m_model.pinned);
    // What doesn't fit goes behind a "…" button before the tray button.
    auto places = island::ArrangeSlots(/*start=*/true, slots, m_taskbarHidden, metrics.visibleCount);
    PlaceTriggers();

    if (!force && m_layout && layout.island == m_layout->island && layout.items == m_layout->items &&
        layout.divider == m_layout->divider && places == m_places && style == m_style && monitor.dpi == m_dpi)
    {
        return;
    }
    if (const auto overflow = places.IndexOf(island::SlotMap::Kind::Overflow);
        overflow && (places != m_places || slots != m_overflowCount))
    {
        m_log.Info(std::format("island: {} of {} items fit; the rest are in the overflow menu",
                               places.entries[*overflow].slot, slots));
    }
    m_places = std::move(places);
    m_overflowCount = slots;

    const auto& island = layout.island;
    // Tucked behind a full-screen app, else above everything.
    // One call, never a hide and a show: the island must stay on screen while it moves (docs/adr/0011).
    SetWindowPos(m_hwnd, m_tucked ? HWND_BOTTOM : HWND_TOPMOST, island.left, island.top, island.Width(),
                 island.Height(), SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    // Clicks on the transparent corners go to the window below.
    const int diameter = 2 * metrics.cornerRadius;
    SetWindowRgn(m_hwnd,
                 CreateRoundRectRgn(0, 0, island.Width() + 1, island.Height() + 1, diameter, diameter), TRUE);

    m_style = style;
    m_highContrast = appearance.highContrast;
    m_dpi = monitor.dpi;
    const bool countChanged = !m_layout || m_layout->items.size() != layout.items.size();
    m_layout = std::move(layout);
    // The items may have moved from under it.
    HideLabel();
    if (m_hovered && *m_hovered >= m_layout->items.size())
    {
        m_hovered.reset();
    }
    if (countChanged && m_accessibility)
    {
        // More or fewer items fit the monitor now.
        m_accessibility->ItemsChanged();
    }
    // Read again on every redraw rather than kept, one at a time: decoded, 40 icons would hold 10 MB (rule
    // 4).
    std::size_t drawn = 0;
    const auto& items = m_model.slots;
    const auto& iconsDir = m_options.iconsDir;
    const adapters::CompositionRenderer::IconSource icons =
        [&](std::size_t index, std::vector<std::uint8_t>& file) -> std::optional<island::IconView> {
        const auto slot = SlotAt(index);
        if (!slot || *slot >= items.size())
        {
            return std::nullopt;
        }
        const auto& icon = items[*slot].icon;
        file = icon ? adapters::ReadIconFile(iconsDir, *icon) : std::vector<std::uint8_t>{};
        auto view = island::ParseIconFile(file);
        drawn += view.has_value() ? 1 : 0;
        return view;
    };

    if (m_renderer.Render(*m_layout, m_style, icons, VisibleMarks(), m_hovered, m_places, kRenderCommitted,
                          ++m_renderGeneration))
    {
        // The items after the Start button: the dock's slots, the "…" and tray buttons.
        m_log.Info(std::format("island: {} items ({} icons) after Start, {}x{} at {},{} ({} dpi, {})",
                               m_layout->items.size() - 1, drawn, island.Width(), island.Height(),
                               island.left, island.top, m_dpi, m_style.backdrop ? "backdrop" : "solid"));
    }
    UpdatePresentable();
}

void DockWindow::OnCommitted(WPARAM generation)
{
    // A frame drawn before the device was lost doesn't count.
    if (generation != m_renderGeneration || m_deviceLost)
    {
        return;
    }
    m_committed = true;
    if (!m_shown)
    {
        m_shown = true;
        // Nothing was visible before the first frame: no flash of an empty window.
        ShowWindow(m_hwnd, SW_SHOWNA);
        PlaceTriggers();
    }
    UpdatePresentable();
}

void DockWindow::OnDeviceLost()
{
    if (!m_deviceLost)
    {
        m_log.Warn("island: the graphics device was lost; rebuilding");
    }
    // What was drawn is gone: the taskbar comes back until the island is drawn again.
    m_deviceLost = true;
    m_committed = false;
    UpdatePresentable();

    if (!m_renderer.RecreateDevice())
    {
        if (++m_deviceFailures >= kDeviceAttempts)
        {
            m_log.Error("island: no graphics device to draw with; exiting");
            RequestExit(exit_code::kGraphicsUnavailable);
            return;
        }
        SetTimer(m_hwnd, kDeviceRetryTimer, kDeviceRetryMs, nullptr);
        return;
    }
    m_deviceFailures = 0;
    m_deviceLost = false;
    Refresh(/*force=*/true);
}

void DockWindow::UpdatePresentable(bool recheck)
{
    // On screen: drawn, visible, not cloaked, and inside one connected monitor (docs/adr/0011).
    island::WindowFacts facts{.committed = m_committed && m_layout.has_value(),
                              .visible = IsWindowVisible(m_hwnd) != FALSE};
    // Tucked behind a full-screen app, it counts only while its trigger can bring it back (docs/adr/0014).
    facts.tucked = m_tucked;
    facts.triggerArmed = DockTriggerArmed();
    DWORD cloaked = 0;
    facts.cloaked =
        SUCCEEDED(DwmGetWindowAttribute(m_hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0;
    if (RECT window{}; GetWindowRect(m_hwnd, &window))
    {
        facts.window = FromRect(window);
    }
    const bool presentable = island::IsOnScreen(facts, ConnectedMonitors());
    if (!presentable && m_presentable && !recheck && facts.committed && facts.visible && !facts.cloaked)
    {
        // Only its place is off: a monitor came or went, and Windows moved the island before this thread
        // heard of it. Look again once the display change (sent, so ahead of this post) has put it back in
        // its place, rather than showing the taskbar for that moment.
        PostMessageW(m_hwnd, kRecheckPresentable, 0, 0);
        return;
    }
    if (presentable != m_presentable)
    {
        m_presentable = presentable;
        m_log.Info(presentable ? "island: shown" : "island: not shown");
        // The taskbar may only be hidden while the island is on screen.
        m_guard.PostPresentable(presentable);
    }
    // Whatever moved, tucked, showed or hid the dock went through here: Harbor Shell's island follows.
    SendDockState();
}

std::optional<std::size_t> DockWindow::ItemAtClient(LPARAM position) const
{
    if (!m_layout)
    {
        return std::nullopt;
    }
    // Client coordinates; the layout is in screen coordinates.
    return ItemAt({m_layout->island.left + static_cast<short>(LOWORD(position)),
                   m_layout->island.top + static_cast<short>(HIWORD(position))});
}

void DockWindow::SetHovered(std::optional<std::size_t> hovered)
{
    if (hovered == m_hovered)
    {
        return;
    }
    m_hovered = hovered;
    m_renderer.SetHovered(hovered);
    if (!hovered)
    {
        HideLabel();
    }
    else if (m_labelItem)
    {
        // Moving along the dock with a label up: the next one at once, like tooltips.
        ShowLabel(*hovered);
    }
    else
    {
        SetTimer(m_hwnd, kLabelTimer, kLabelDelayMs, nullptr);
    }
}

void DockWindow::ShowLabel(std::size_t index)
{
    KillTimer(m_hwnd, kLabelTimer);
    if (m_label == nullptr || m_menuOpen || !m_layout || index >= m_layout->items.size() ||
        !IsWindowVisible(m_hwnd))
    {
        return;
    }
    const auto size = m_renderer.RenderLabel(ItemName(index), m_style, m_dpi);
    if (!size)
    {
        HideLabel();
        return;
    }
    // Centred above the item, inside its monitor.
    const auto& item = m_layout->items[index];
    const auto monitor = m_hosts.empty() ? m_layout->island : m_hosts[DockHost()].monitor.bounds;
    const int width = static_cast<int>(size->cx);
    const int height = static_cast<int>(size->cy);
    const int x = std::clamp((item.left + item.right - width) / 2, monitor.left,
                             std::max(monitor.left, monitor.right - width));
    const int y = m_layout->island.top - island::Scale(6, m_dpi) - height;
    SetWindowPos(m_label, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    KillTimer(m_hwnd, kLabelReleaseTimer);
    if (m_labelItem != index)
    {
        m_labelItem = index;
        // Which item, never what it says.
        const auto slot = SlotAt(index);
        const char* button = IsKind(index, island::SlotMap::Kind::Start)  ? "Start"
                             : IsKind(index, island::SlotMap::Kind::Tray) ? "tray"
                                                                          : "overflow";
        m_log.Info(slot ? std::format("label: shown for item {}", *slot)
                        : std::format("label: shown for the {} button", button));
    }
}

void DockWindow::HideLabel()
{
    KillTimer(m_hwnd, kLabelTimer);
    if (!m_labelItem)
    {
        return;
    }
    m_labelItem.reset();
    ShowWindow(m_label, SW_HIDE);
    // Its font and bitmap go after a quiet while (rule 4).
    SetTimer(m_hwnd, kLabelReleaseTimer, kLabelReleaseMs, nullptr);
}

void DockWindow::OnMouseMove(LPARAM position)
{
    if (!m_trackingMouse)
    {
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, m_hwnd, 0};
        m_trackingMouse = TrackMouseEvent(&track) != FALSE;
    }
    ReportPointer(true);
    SetHovered(ItemAtClient(position));
}

void DockWindow::OnClick(LPARAM position)
{
    HideLabel();
    if (const auto item = ItemAtClient(position))
    {
        ClickItem(*item, "click");
    }
}

bool DockWindow::ClickItem(std::size_t index, std::string_view how)
{
    if (!m_layout || index >= m_layout->items.size() || index >= m_places.entries.size())
    {
        return false;
    }
    const auto& place = m_places.entries[index];
    switch (place.kind)
    {
    case island::SlotMap::Kind::Start:
        OpenStart(how);
        return true;
    case island::SlotMap::Kind::Tray:
        PeekTaskbar(how);
        return true;
    case island::SlotMap::Kind::Overflow:
        OpenOverflowMenu(how);
        return true;
    case island::SlotMap::Kind::Slot:
        return ClickSlot(place.slot, how);
    }
    return false;
}

std::optional<std::size_t> DockWindow::SlotAt(std::size_t index) const
{
    if (index >= m_places.entries.size() || m_places.entries[index].kind != island::SlotMap::Kind::Slot)
    {
        return std::nullopt;
    }
    return m_places.entries[index].slot;
}

bool DockWindow::IsKind(std::size_t index, island::SlotMap::Kind kind) const
{
    return index < m_places.entries.size() && m_places.entries[index].kind == kind;
}

void DockWindow::OpenWindowsStart(std::string_view how)
{
    // Until the island opens instead (docs/adr/0015): Windows' Start, as the Windows key would. Windows
    // documents SC_TASKLIST as "activates the Start menu"; no keys are injected, so a held Shift can't make
    // it Ctrl+Shift+Esc, and an elevated window in front doesn't stop it.
    if (!m_options.takesForeground)
    {
        m_log.Info(std::format("click: Start ({}): Windows Start (a test dock: not opened)", how));
        return;
    }
    PostMessageW(m_hwnd, WM_SYSCOMMAND, SC_TASKLIST, 0);
    m_log.Info(std::format("click: Start ({}): Windows Start", how));
}

void DockWindow::OpenStart(std::string_view how)
{
    if (!m_island.ready || !m_island.hotkey)
    {
        OpenWindowsStart(how);
        return;
    }
    PressIslandHotkey(*m_island.hotkey, how);
    KillTimer(m_hwnd, kIslandTimer);
    m_islandAskedAt.reset();
    // Open already: the same key closes it, nothing to wait for.
    if (!m_island.open)
    {
        m_islandAskedAt = SteadyNow();
        SetTimer(m_hwnd, kIslandTimer, kIslandAnswerMs, nullptr);
    }
}

void DockWindow::PressIslandHotkey(std::uint32_t key, std::string_view how)
{
    if (!m_options.takesForeground)
    {
        m_log.Info(std::format("click: Start ({}): the island's hotkey (a test dock: not pressed)", how));
        return;
    }
    // Pressed and released at once, in one SendInput: nothing else can come between them. The hotkey's owner,
    // Harbor Shell, may take the foreground for it (M4 spike).
    INPUT keys[2]{};
    for (auto& input : keys)
    {
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = static_cast<WORD>(key);
    }
    keys[1].ki.dwFlags = KEYEVENTF_KEYUP;
    const UINT sent = SendInput(2, keys, sizeof(INPUT));
    m_log.Info(sent == 2 ? std::format("click: Start ({}): the island's hotkey", how)
                         : std::format("click: Start ({}): the island's hotkey wasn't pressed ({})", how,
                                       GetLastError()));
}

void DockWindow::OnIslandState(const ipc::IslandState& state)
{
    if (state == m_island)
    {
        return;
    }
    const bool openChanged = state.open != m_island.open;
    if (state.ready != m_island.ready)
    {
        m_log.Info(state.ready ? "island: ready" : "island: not ready");
    }
    if (openChanged)
    {
        m_log.Info(state.open ? "island: open" : "island: closed");
    }
    m_island = state;
    if (state.open && m_islandAskedAt)
    {
        m_log.Info(std::format("island: opened {} ms after the Start button",
                               (SteadyNow() - *m_islandAskedAt).count()));
        m_islandAskedAt.reset();
        KillTimer(m_hwnd, kIslandTimer);
    }
    if (openChanged)
    {
        // The Start button's dot shows the island is open.
        m_renderer.SetMarks(VisibleMarks());
    }
}

void DockWindow::OnIslandLaunch(const ipc::AppTarget& item)
{
    if (!m_launcher)
    {
        return;
    }
    // The user asked, in the island: whatever starts may take the foreground. Which app is the user's
    // business.
    AllowSetForegroundWindow(ASFW_ANY);
    m_log.Info("launch: from the island");
    m_launcher->Launch(kIslandLaunch, ipc::DockItem{item.kind, item.target, {}, std::nullopt});
}

void DockWindow::SendDockState()
{
    if (!m_connected || m_hosts.empty())
    {
        return;
    }
    const auto& host = m_hosts[DockHost()];
    // Within the contract's bounds, whatever Windows says.
    const auto clamp = [](const geometry::Rect& rect) {
        const auto side = [](int value) {
            return std::clamp(value, -ipc::DockState::kMaxCoordinate, ipc::DockState::kMaxCoordinate);
        };
        return geometry::Rect{side(rect.left), side(rect.top), side(rect.right), side(rect.bottom)};
    };
    const ipc::DockState state{
        .monitor = clamp(host.monitor.bounds),
        .work = clamp(host.monitor.work),
        .dpi = std::clamp(host.monitor.dpi, ipc::DockState::kMinDpi, ipc::DockState::kMaxDpi),
        .fullScreen = host.fullScreen.IsOpen(),
        .visible = m_shown && IsWindowVisible(m_hwnd) != FALSE,
    };
    if (state == m_sentDockState)
    {
        return;
    }
    m_sentDockState = state;
    m_controlChannel.Send(state);
}

bool DockWindow::ClickSlot(std::size_t index, std::string_view how)
{
    if (index >= m_model.slots.size())
    {
        return false;
    }
    // Which window is in front now, not when the last snapshot came.
    std::optional<windows::WindowId> front;
    if (const HWND foreground = GetForegroundWindow(); foreground != nullptr)
    {
        front = ToId(GetAncestor(foreground, GA_ROOTOWNER));
    }
    const auto action = island::DecideClick(WindowsOf(m_model.slots[index]), front);
    // Never SendMessage to another app's window: a hung one would stall this thread (docs/adr/0010).
    const HWND window = ToHwnd(action.window);
    switch (action.kind)
    {
    case island::ClickAction::Kind::Launch:
        return LaunchItem(index, how);
    case island::ClickAction::Kind::Activate:
        BringForward(action.window, action.restore);
        m_log.Info(std::format("click: item {} ({}): a window comes forward{}", index, how,
                               action.restore ? ", restored" : ""));
        return true;
    case island::ClickAction::Kind::Minimize:
        PostMessageW(window, WM_SYSCOMMAND, SC_MINIMIZE, 0);
        m_log.Info(std::format("click: item {} ({}): its window is minimized", index, how));
        return true;
    }
    return false;
}

void DockWindow::BringForward(windows::WindowId id, bool restore)
{
    const HWND window = ToHwnd(id);
    // Windows may flash a window it restores this way: that isn't the app asking for attention.
    if (m_watcher)
    {
        m_watcher->NoteActivation(id);
    }
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    // A click gave the dock the right to change the foreground: pass it on to the app.
    AllowSetForegroundWindow(process);
    if (restore)
    {
        // Posted: unlike ShowWindowAsync it also works on an elevated app's window.
        PostMessageW(window, WM_SYSCOMMAND, SC_RESTORE, 0);
    }
    SetForegroundWindow(window);
}

void DockWindow::PeekTaskbar(std::string_view how)
{
    const HWND taskbar = m_guard.Peek();
    if (taskbar == nullptr)
    {
        m_log.Info(std::format("click: the tray button ({}): the taskbar isn't hidden", how));
        return;
    }
    // From the thread that took the click, which may change the foreground (M3 spike). Explorer shows its
    // taskbar when it gets the foreground, and slides it away when it loses it.
    const bool forward = SetForegroundWindow(taskbar) != FALSE;
    m_log.Info(
        std::format("click: the tray button ({}): {}", how,
                    forward ? "the taskbar comes forward" : "Windows didn't let the taskbar come forward"));
}

void DockWindow::OnRightClick(LPARAM position)
{
    if (const auto item = ItemAtClient(position))
    {
        OpenMenu(*item, "right-click");
    }
}

void DockWindow::OpenOverflowMenu(std::string_view how)
{
    const auto at = m_places.IndexOf(island::SlotMap::Kind::Overflow);
    if (!m_layout || !at || *at >= m_layout->items.size() || m_menuOpen)
    {
        return;
    }
    // Copies, as for OpenMenu. The slots from first on are behind the button.
    const auto first = m_places.entries[*at].slot;
    std::vector<std::string> names;
    std::vector<std::vector<std::uint8_t>> icons;
    const auto side = static_cast<std::uint32_t>(island::Scale(16, m_dpi));
    for (auto slot = m_model.slots.begin() + static_cast<std::ptrdiff_t>(first); slot != m_model.slots.end();
         ++slot)
    {
        names.push_back(slot->name);
        // Read one at a time and kept only as small as the menu shows them.
        const auto file = slot->icon ? adapters::ReadIconFile(m_options.iconsDir, *slot->icon)
                                     : std::vector<std::uint8_t>{};
        const auto icon = island::ParseIconFile(file);
        icons.push_back(icon ? island::Downscale(*icon, side) : std::vector<std::uint8_t>{});
    }
    const auto menu = island::BuildOverflowMenu(names, first);
    const auto& button = m_layout->items[*at];
    const auto& island = m_layout->island;
    HideLabel();
    // An open menu keeps the island on top over a full-screen app.
    ReportPointer(true);
    m_menuOpen = true;
    m_overflowMenuOpen = true;
    const auto chosen = adapters::PopupMenu::Show(
        m_hwnd, menu, {(button.left + button.right) / 2, island.top},
        {island.left, island.top, island.right, island.bottom},
        {.style = m_style, .dpi = m_dpi, .highContrast = m_highContrast}, icons, static_cast<int>(side));
    m_overflowMenuOpen = false;
    m_menuOpen = false;
    ReportPointer(PointerOverDock());
    m_log.Info(std::format("menu: the overflow menu ({}, {} items){}", how, names.size(),
                           chosen ? std::format(": item {} chosen", menu[*chosen].item) : ""));
    if (chosen)
    {
        ClickSlot(menu[*chosen].item, "overflow menu");
    }
}

void DockWindow::OpenMenu(std::size_t index, std::string_view how)
{
    if (IsKind(index, island::SlotMap::Kind::Overflow))
    {
        OpenOverflowMenu(how);
        return;
    }
    // The Start and tray buttons have no menu.
    const auto slotIndex = SlotAt(index);
    if (!m_layout || index >= m_layout->items.size() || !slotIndex || *slotIndex >= m_model.slots.size() ||
        m_menuOpen)
    {
        return;
    }
    // Copies: the model can change while the menu is open (its loop keeps handling this window's messages).
    const auto slot = m_model.slots[*slotIndex];
    auto windows = WindowsOf(slot);
    std::sort(windows.begin(), windows.end(), [](const auto& a, const auto& b) { return a.seen < b.seen; });
    std::optional<ipc::DockItem> pin;
    if (slot.kind == island::DockSlot::Kind::Pin)
    {
        pin = m_items.items[slot.pin];
    }
    // What the Shell said about the app: how to start it, and what pinning it pins.
    std::optional<ipc::AppTarget> launch;
    for (const auto app : slot.apps)
    {
        if (const auto info = m_appInfo.find(app); info != m_appInfo.end() && info->second.launch)
        {
            launch = info->second.launch;
            break;
        }
    }
    island::MenuFacts facts{.name = slot.name,
                            .pinned = pin.has_value(),
                            .pinnable = pin.has_value() || launch.has_value(),
                            .launchable = pin.has_value() || launch.has_value()};
    for (const auto& window : windows)
    {
        // Read now, never logged.
        facts.windows.push_back({window.id, TitleOf(ToHwnd(window.id))});
    }
    const auto menu = island::BuildAppMenu(facts);

    const auto& item = m_layout->items[index];
    const auto& island = m_layout->island;
    HideLabel();
    ReportPointer(true);
    m_menuOpen = true;
    const auto chosen =
        adapters::PopupMenu::Show(m_hwnd, menu, {(item.left + item.right) / 2, island.top},
                                  {island.left, island.top, island.right, island.bottom},
                                  {.style = m_style, .dpi = m_dpi, .highContrast = m_highContrast});
    m_menuOpen = false;
    ReportPointer(PointerOverDock());
    if (!chosen)
    {
        return;
    }

    const auto& command = menu[*chosen];
    // Which item and what was done, never which app or window.
    m_log.Info(std::format("menu: item {} ({}): {}", *slotIndex, how, Describe(command.command)));
    switch (command.command)
    {
    case island::MenuCommand::Title:
        break;
    case island::MenuCommand::Window:
        for (const auto& window : windows)
        {
            if (window.id == command.window)
            {
                BringForward(window.id, window.minimized);
            }
        }
        break;
    case island::MenuCommand::Launch:
        if (m_launcher)
        {
            AllowSetForegroundWindow(ASFW_ANY);
            m_launcher->Launch(*slotIndex,
                               pin ? *pin
                                   : ipc::DockItem{launch->kind, launch->target, slot.name, std::nullopt});
        }
        break;
    case island::MenuCommand::Pin:
        // Nothing changes here until Harbor Shell sends the new list.
        m_controlChannel.Send(ipc::PinRequest{ipc::PinAction::Pin, *launch});
        break;
    case island::MenuCommand::Unpin:
        m_controlChannel.Send(ipc::PinRequest{ipc::PinAction::Unpin, {pin->kind, pin->target}});
        break;
    case island::MenuCommand::CloseWindow:
        // Posted: the app asks about unsaved work as it would for its own close button.
        PostMessageW(ToHwnd(command.window), WM_CLOSE, 0, 0);
        break;
    case island::MenuCommand::CloseAll:
        for (const auto& window : windows)
        {
            PostMessageW(ToHwnd(window.id), WM_CLOSE, 0, 0);
        }
        break;
    }
}

bool DockWindow::LaunchItem(std::size_t index, std::string_view how)
{
    if (index >= m_model.slots.size() || m_model.slots[index].kind != island::DockSlot::Kind::Pin ||
        !m_launcher)
    {
        // Until Harbor Shell says how, only pinned items can be started.
        return false;
    }
    // The user asked: whatever starts may take the foreground (Explorer starts it, not the dock).
    AllowSetForegroundWindow(ASFW_ANY);
    // Which item, not which app: the pinned list is the user's business.
    m_log.Info(std::format("launch: item {} ({})", index, how));
    m_launcher->Launch(index, m_items.items[m_model.slots[index].pin]);
    return true;
}

void DockWindow::AssignItemKeys()
{
    std::map<std::string, std::uint64_t> keys;
    m_itemKeys.clear();
    for (const auto& slot : m_model.slots)
    {
        // A pin by what it starts (the same pin twice is two items: each later one's identity gets another
        // line ending); a running app by its id.
        auto identity = slot.kind == island::DockSlot::Kind::Pin
                            ? std::format("pin\n{}\n{}\n", static_cast<int>(m_items.items[slot.pin].kind),
                                          m_items.items[slot.pin].target)
                            : std::format("app\n{}\n", slot.apps.front());
        while (keys.contains(identity))
        {
            identity.push_back('\n');
        }
        const auto previous = m_keysByIdentity.find(identity);
        const auto key = previous != m_keysByIdentity.end() ? previous->second : m_nextItemKey++;
        keys.emplace(std::move(identity), key);
        m_itemKeys.push_back(key);
    }
    m_keysByIdentity = std::move(keys);
}

std::size_t DockWindow::ItemCount() const
{
    return m_layout ? m_layout->items.size() : 0;
}

std::uint64_t DockWindow::ItemKey(std::size_t index) const
{
    if (IsKind(index, island::SlotMap::Kind::Start))
    {
        return kStartKey;
    }
    if (IsKind(index, island::SlotMap::Kind::Tray))
    {
        return kTrayKey;
    }
    if (IsKind(index, island::SlotMap::Kind::Overflow))
    {
        return kOverflowKey;
    }
    const auto slot = SlotAt(index);
    return slot && *slot < m_itemKeys.size() ? m_itemKeys[*slot] : 0;
}

std::optional<std::size_t> DockWindow::IndexOfKey(std::uint64_t key) const
{
    if (key == kStartKey)
    {
        return m_places.IndexOf(island::SlotMap::Kind::Start);
    }
    if (key == kOverflowKey)
    {
        return m_places.IndexOf(island::SlotMap::Kind::Overflow);
    }
    if (key == kTrayKey)
    {
        return m_places.IndexOf(island::SlotMap::Kind::Tray);
    }
    // Items behind the "…" button aren't on the island.
    for (std::size_t slot = 0; slot < m_itemKeys.size(); ++slot)
    {
        if (m_itemKeys[slot] == key)
        {
            const auto place = m_places.IndexOfSlot(slot);
            return place && *place < ItemCount() ? place : std::nullopt;
        }
    }
    return std::nullopt;
}

bool DockWindow::IsOverflow(std::size_t index) const
{
    return IsKind(index, island::SlotMap::Kind::Overflow);
}

bool DockWindow::IsOverflowMenuOpen() const
{
    return m_overflowMenuOpen;
}

void DockWindow::CloseMenu()
{
    EndMenu();
}

std::wstring DockWindow::ItemName(std::size_t index) const
{
    if (IsKind(index, island::SlotMap::Kind::Start))
    {
        return L"Start";
    }
    if (IsKind(index, island::SlotMap::Kind::Tray))
    {
        return L"Show taskbar";
    }
    if (IsKind(index, island::SlotMap::Kind::Overflow))
    {
        return L"More apps";
    }
    const auto slot = SlotAt(index);
    if (!slot || *slot >= m_model.slots.size())
    {
        return {};
    }
    const auto& name = m_model.slots[*slot].name;
    const int length =
        MultiByteToWideChar(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(std::max(length, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), wide.data(), length);
    return wide;
}

std::wstring DockWindow::ItemStatus(std::size_t index) const
{
    if (IsKind(index, island::SlotMap::Kind::Start))
    {
        return m_island.open ? L"island open" : L"";
    }
    if (IsKind(index, island::SlotMap::Kind::Overflow))
    {
        const auto first = m_places.entries[index].slot;
        return std::format(L"{} more apps", m_model.slots.size() - std::min(first, m_model.slots.size()));
    }
    const auto slot = SlotAt(index);
    if (!slot || *slot >= m_model.slots.size())
    {
        // The Start and tray buttons.
        return {};
    }
    const auto windows = WindowsOf(m_model.slots[*slot]).size();
    if (windows == 0)
    {
        return {};
    }
    std::wstring status = windows == 1 ? L"running, 1 window" : std::format(L"running, {} windows", windows);
    if (m_model.slots[*slot].marks.attention)
    {
        status += L", needs attention";
    }
    return status;
}

RECT DockWindow::ItemBounds(std::size_t index) const
{
    if (!m_layout || index >= m_layout->items.size())
    {
        return {};
    }
    const auto& item = m_layout->items[index];
    return {item.left, item.top, item.right, item.bottom};
}

std::optional<std::size_t> DockWindow::ItemAt(POINT screenPoint) const
{
    return m_layout ? island::HitTest(*m_layout, {screenPoint.x, screenPoint.y}) : std::nullopt;
}

bool DockWindow::InvokeItem(std::size_t index)
{
    if (IsKind(index, island::SlotMap::Kind::Overflow))
    {
        // Posted: a menu's loop mustn't run inside the screen reader's call.
        return PostMessageW(m_hwnd, kOpenOverflowMessage, 0, 0) != FALSE;
    }
    return ClickItem(index, "UI Automation");
}

void DockWindow::SyncHosts()
{
    const auto monitors = ReadMonitors();
    const auto before = m_hosts.size();
    bool changed = false;
    // A host whose monitor went, or changed size, goes (with its AppBar).
    for (auto it = m_hosts.begin(); it != m_hosts.end();)
    {
        if (std::none_of(monitors.begin(), monitors.end(), [&](const island::MonitorInfo& monitor) {
                return monitor.bounds == it->monitor.bounds;
            }))
        {
            StopHost(*it);
            it = m_hosts.erase(it);
            changed = true;
        }
        else
        {
            ++it;
        }
    }
    for (const auto& monitor : monitors)
    {
        const auto known = std::find_if(m_hosts.begin(), m_hosts.end(), [&](const MonitorHost& host) {
            return host.monitor.bounds == monitor.bounds;
        });
        if (known != m_hosts.end())
        {
            // Its work area, DPI or primary-ness may have changed.
            known->monitor = monitor;
            continue;
        }
        MonitorHost host{.monitor = monitor};
        // Never activated; nearly transparent rather than invisible, so the cursor still hits it.
        host.window =
            CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_LAYERED, kHostClass,
                            L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, m_instance, this);
        if (host.window == nullptr)
        {
            m_log.Warn(std::format("appbar: no host window for a monitor ({})", GetLastError()));
            continue;
        }
        SetLayeredWindowAttributes(host.window, 0, 1, LWA_ALPHA);
        // Test docks reserve nothing: tests never change the user's work area.
        std::unique_ptr<appbar::IAppBar> appBar;
        if (m_options.testAppBar)
        {
            appBar = std::make_unique<adapters::TestAppBar>();
        }
        else
        {
            appBar = std::make_unique<adapters::Win32AppBar>(host.window, kAppBarCallback);
        }
        // Registered only to hear of full-screen apps (ABN_FULLSCREENAPP): it asks for no strip, so Explorer
        // grants none (docs/adr/0014).
        const HWND window = host.window;
        host.appBar = std::make_unique<AppBarWorker>(std::move(appBar), [window](const geometry::Rect&) {
            PostMessageW(window, kAppBarGranted, 0, 0);
        });
        host.appBar->SetWant({monitor.bounds, /*height=*/0});
        m_hosts.push_back(std::move(host));
        changed = true;
    }
    if (changed)
    {
        m_log.Info(std::format("appbar: {} monitors (was {}), notify-only on each: no strip", m_hosts.size(),
                               before));
        // The island's monitor may have gone with its full-screen app.
        OnFullScreenChanged();
    }
}

void DockWindow::StopHost(MonitorHost& host) noexcept
{
    if (host.appBar)
    {
        host.appBar->Stop();
        host.appBar.reset();
    }
    if (host.window != nullptr)
    {
        DestroyWindow(host.window);
        host.window = nullptr;
    }
}

std::size_t DockWindow::DockHost() const
{
    std::vector<island::MonitorInfo> monitors;
    for (const auto& host : m_hosts)
    {
        monitors.push_back(host.monitor);
    }
    return island::DockMonitor(monitors, m_dockMonitor).value_or(0);
}

geometry::Rect DockWindow::StripOf(const MonitorHost& host, int height) const
{
    // Above a visible taskbar; at the monitor's bottom edge while Harbor hides it (auto-hide gives the work
    // area back). Over maximized windows: nothing reserves it.
    return {host.monitor.bounds.left, host.monitor.work.bottom - height, host.monitor.bounds.right,
            host.monitor.work.bottom};
}

void DockWindow::PlaceTriggers()
{
    std::vector<island::MonitorInfo> monitors;
    for (const auto& host : m_hosts)
    {
        monitors.push_back(host.monitor);
    }
    const auto dock = DockHost();
    for (std::size_t i = 0; i < m_hosts.size(); ++i)
    {
        auto& host = m_hosts[i];
        // Not while off (panic). Elsewhere, not over a full-screen app (a game, say); on the dock's own
        // monitor, only over one: there it brings the tucked island back (docs/adr/0014).
        const bool wanted =
            i == dock ? host.fullScreen.IsOpen() && !m_testTriggerBroken : !host.fullScreen.IsOpen();
        std::vector<geometry::Rect> triggers;
        if (wanted && m_triggersOn && m_shown)
        {
            const auto height = static_cast<int>(island::Scale(
                island::kIconDip + 2 * island::kPaddingDip + 2 * island::kMarginDip, host.monitor.dpi));
            // Over a full-screen app the monitor's bottom edge is the app's, even with a taskbar under it:
            // the trigger sits there, never over that taskbar.
            const auto& bounds = host.monitor.bounds;
            const auto strip =
                i == dock ? geometry::Rect{bounds.left, bounds.bottom - height, bounds.right, bounds.bottom}
                          : StripOf(host, height);
            triggers = island::EdgeTriggers(monitors, i, strip, kTriggerRaise);
        }
        if (triggers.empty())
        {
            ShowWindow(host.window, SW_HIDE);
            continue;
        }
        // The widest stretch of the edge; the window reaches from the trigger row down to the monitor's edge,
        // so a cursor that jumps past the row still lands on it.
        const auto widest =
            *std::max_element(triggers.begin(), triggers.end(),
                              [](const auto& a, const auto& b) { return a.Width() < b.Width(); });
        SetWindowPos(host.window, HWND_TOPMOST, widest.left, widest.top, widest.Width(),
                     host.monitor.bounds.bottom - widest.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
}

void DockWindow::OnHostMouseMove(std::size_t index)
{
    const bool here = index == DockHost();
    if (!m_triggersOn || (here && !m_tuck.FullScreen()) || m_windows.movingOrSizing || m_menuOpen ||
        (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 || (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0)
    {
        return;
    }
    if (here)
    {
        // Leaving it may let the island tuck again.
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, m_hosts[index].window, 0};
        TrackMouseEvent(&track);
    }
    if (m_triggerHost != index)
    {
        // The cursor must rest there a moment: passing by on the way to another monitor doesn't count.
        m_triggerHost = index;
        SetTimer(m_hwnd, kEdgeTimer, kEdgeRestMs, nullptr);
    }
}

void DockWindow::OnEdgeTimer()
{
    KillTimer(m_hwnd, kEdgeTimer);
    const auto index = std::exchange(m_triggerHost, std::nullopt);
    if (!index || *index >= m_hosts.size() || !m_triggersOn || m_windows.movingOrSizing ||
        (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 || (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0)
    {
        return;
    }
    POINT cursor{};
    RECT trigger{};
    const auto& host = m_hosts[*index];
    if (!GetCursorPos(&cursor) || !GetWindowRect(host.window, &trigger) ||
        !island::RestsOnTrigger(FromRect(trigger), host.monitor, {cursor.x, cursor.y}))
    {
        return;
    }
    if (*index == DockHost())
    {
        // The dock's own monitor: the island comes back over the full-screen app.
        ReportPointer(true);
        return;
    }
    MoveDock(*index);
}

void DockWindow::MoveDock(std::size_t index)
{
    m_dockMonitor = m_hosts[index].monitor.bounds;
    HideLabel();
    // Which monitor in the list, not where: positions say little, and nothing about the user.
    m_log.Info(std::format("island: moves to monitor {} of {}", index + 1, m_hosts.size()));
    m_hovered.reset();
    Refresh(/*force=*/true);
    OnFullScreenChanged();
}

void DockWindow::OnAppBarNotification(std::size_t index, WPARAM notification, LPARAM lParam)
{
    switch (notification)
    {
    case ABN_POSCHANGED:
        // The taskbar or another AppBar changed, so may the work areas. Explorer sends these in bursts; the
        // hosts are looked at outside this host's window procedure (a monitor may have gone with its host).
        PostMessageW(m_hwnd, kWorkAreaMayHaveChanged, 0, 0);
        break;
    case ABN_FULLSCREENAPP:
        OnFullScreenNotice(index, lParam != 0);
        break;
    case ABN_STATECHANGE:
        // The taskbar's auto-hide or always-on-top changed. Harbor sets auto-hide itself while it hides the
        // taskbar, so this can't tell the user's change from its own (revisited with onboarding, M5).
        m_log.Info("appbar: the taskbar's state changed");
        break;
    default:
        break;
    }
}

void DockWindow::OnFullScreenNotice(std::size_t index, bool open)
{
    auto& host = m_hosts[index];
    if (host.fullScreen.IsOpen() == open)
    {
        return;
    }
    if (open)
    {
        // The window in front is the app, if it covers the monitor (docs/adr/0014).
        host.fullScreen.Open(host.monitor.bounds, JudgedForeground());
    }
    else
    {
        host.fullScreen.Close();
    }
    // Which monitor, never which app.
    m_log.Info(
        std::format("appbar: monitor {}: a full-screen app {}", index + 1, open ? "opened" : "closed"));
    OnFullScreenChanged();
}

void DockWindow::LookAgainAtFullScreen()
{
    if (std::none_of(m_hosts.begin(), m_hosts.end(),
                     [](const MonitorHost& host) { return host.fullScreen.IsOpen(); }))
    {
        return;
    }
    const auto foreground = JudgedForeground();
    bool ended = false;
    for (std::size_t i = 0; i < m_hosts.size(); ++i)
    {
        auto& host = m_hosts[i];
        if (host.fullScreen.IsOpen() && !host.fullScreen.LookAgain(host.monitor.bounds, foreground))
        {
            m_log.Info(
                std::format("appbar: monitor {}: the full-screen app ended (Explorer didn't say)", i + 1));
            ended = true;
        }
    }
    if (ended)
    {
        OnFullScreenChanged();
    }
}

std::optional<island::Foreground> DockWindow::JudgedForeground() const
{
    const HWND foreground = GetForegroundWindow();
    RECT bounds{};
    if (foreground == nullptr || !GetWindowRect(foreground, &bounds))
    {
        return std::nullopt;
    }
    if (!m_options.windowClassPrefix.empty())
    {
        // Test docks judge only their own test windows: the user's never end a test's full screen.
        std::wstring name(256, L'\0');
        name.resize(static_cast<std::size_t>(std::max(GetClassNameW(foreground, name.data(), 256), 0)));
        const std::wstring prefix(m_options.windowClassPrefix.begin(), m_options.windowClassPrefix.end());
        if (!name.starts_with(prefix))
        {
            return std::nullopt;
        }
    }
    return island::Foreground{ToId(foreground), FromRect(bounds)};
}

void DockWindow::OnFullScreenChanged()
{
    if (m_hosts.empty())
    {
        return;
    }
    const auto& host = m_hosts[DockHost()];
    if (m_tuckMonitor != host.monitor.bounds)
    {
        // What was full screen on the old monitor doesn't apply here.
        m_tuck.OnMonitorChanged();
        m_tuckMonitor = host.monitor.bounds;
    }
    m_tuck.OnFullScreen(host.fullScreen.IsOpen(), SteadyNow());
    // Looked at again every second while any monitor has one: no timer otherwise.
    if (std::any_of(m_hosts.begin(), m_hosts.end(),
                    [](const MonitorHost& each) { return each.fullScreen.IsOpen(); }))
    {
        SetTimer(m_hwnd, kFullScreenTimer, kFullScreenLookMs, nullptr);
    }
    else
    {
        KillTimer(m_hwnd, kFullScreenTimer);
    }
    ApplyTuck();
}

void DockWindow::ReportPointer(bool over)
{
    const auto wakeAt = m_tuck.WakeAt();
    const bool tucked = m_tuck.Tucked();
    m_tuck.OnPointer(over, SteadyNow());
    if (m_tuck.WakeAt() != wakeAt || m_tuck.Tucked() != tucked)
    {
        ApplyTuck();
    }
}

bool DockWindow::PointerOverDock() const
{
    POINT cursor{};
    if (!GetCursorPos(&cursor))
    {
        return false;
    }
    const geometry::Point point{cursor.x, cursor.y};
    // Behind a full-screen app, the island's place is the app's: only its trigger brings it back.
    RECT island{};
    if (!m_tucked && GetWindowRect(m_hwnd, &island) && FromRect(island).Contains(point))
    {
        return true;
    }
    RECT trigger{};
    const HWND own = m_hosts.empty() ? nullptr : m_hosts[DockHost()].window;
    return own != nullptr && IsWindowVisible(own) && GetWindowRect(own, &trigger) &&
           FromRect(trigger).Contains(point);
}

bool DockWindow::DockTriggerArmed() const
{
    if (m_hosts.empty())
    {
        return false;
    }
    const auto& host = m_hosts[DockHost()];
    RECT trigger{};
    return m_triggersOn && host.window != nullptr && IsWindowVisible(host.window) &&
           GetWindowRect(host.window, &trigger) && island::TriggerAtEdge(FromRect(trigger), host.monitor);
}

void DockWindow::ApplyTuck()
{
    // Its monitor's trigger first, so a tucked island is never without one (rule 7, docs/adr/0014).
    PlaceTriggers();
    if (const bool tucked = m_tuck.Tucked(); tucked != m_tucked)
    {
        m_tucked = tucked;
        if (tucked)
        {
            HideLabel();
        }
        m_log.Info(tucked ? "island: tucked behind a full-screen app" : "island: on top again");
        // Behind the app, still shown: the window never hides for it.
        SetWindowPos(m_hwnd, tucked ? HWND_BOTTOM : HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    KillTimer(m_hwnd, kTuckTimer);
    if (const auto wakeAt = m_tuck.WakeAt())
    {
        const auto delay = std::max<std::int64_t>((*wakeAt - SteadyNow()).count(), 1);
        SetTimer(m_hwnd, kTuckTimer, static_cast<UINT>(delay), nullptr);
    }
    UpdatePresentable();
}

LRESULT CALLBACK DockWindow::HostProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE)
    {
        SetWindowLongPtrW(
            hwnd, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
    }
    auto* self = reinterpret_cast<DockWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr)
    {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    const auto host = std::find_if(self->m_hosts.begin(), self->m_hosts.end(),
                                   [hwnd](const MonitorHost& each) { return each.window == hwnd; });
    if (host == self->m_hosts.end())
    {
        // Being made or going away.
        return message == WM_MOUSEACTIVATE ? MA_NOACTIVATE : DefWindowProcW(hwnd, message, wParam, lParam);
    }
    const auto index = static_cast<std::size_t>(host - self->m_hosts.begin());
    switch (message)
    {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_MOUSEMOVE:
        self->OnHostMouseMove(index);
        return 0;
    case WM_MOUSELEAVE:
        if (index == self->DockHost())
        {
            self->ReportPointer(self->PointerOverDock());
        }
        return 0;
    case WM_WINDOWPOSCHANGED:
        // The dock's own trigger shown, hidden or moved: a tucked island may depend on it (rule 7).
        if (index == self->DockHost())
        {
            self->UpdatePresentable();
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    case kAppBarCallback:
        self->OnAppBarNotification(index, wParam, lParam);
        return 0;
    case kAppBarGranted:
        self->m_log.Warn(
            std::format("appbar: monitor {} was granted a strip, though none was asked for", index + 1));
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

void DockWindow::OnLaunched(LaunchReport* report)
{
    const std::unique_ptr<LaunchReport> owned(report);
    const auto& result = owned->result;
    // Which item, or that the island asked; never which app.
    const auto what =
        owned->index == kIslandLaunch ? std::string("from the island") : std::format("item {}", owned->index);
    if (FAILED(result.result))
    {
        m_log.Warn(std::format("launch: {} failed: 0x{:08X} (Explorer: 0x{:08X})", what,
                               static_cast<std::uint32_t>(result.result),
                               static_cast<std::uint32_t>(result.explorerResult)));
    }
    else if (result.route == adapters::LaunchRoute::Explorer)
    {
        m_log.Info(std::format("launch: {} started via Explorer", what));
    }
    else
    {
        m_log.Info(std::format("launch: {} started via ShellExecuteEx (Explorer: 0x{:08X})", what,
                               static_cast<std::uint32_t>(result.explorerResult)));
    }
}

LRESULT CALLBACK DockWindow::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE)
    {
        auto* self = static_cast<DockWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    // Messages that arrive before WM_NCCREATE (e.g. WM_GETMINMAXINFO) have no instance yet.
    auto* self = reinterpret_cast<DockWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr)
    {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    if (message == WM_NCDESTROY)
    {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        self->m_hwnd = nullptr;
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    return self->HandleMessage(message, wParam, lParam);
}

LRESULT DockWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == m_taskbarCreatedMessage && message != 0)
    {
        // Explorer restarted (forgetting every AppBar), or the primary monitor's DPI changed.
        for (auto& host : m_hosts)
        {
            host.appBar->TaskbarCreated();
        }
        if (m_watcher)
        {
            m_watcher->Rescan();
        }
        Refresh(/*force=*/false);
        return 0;
    }

    switch (message)
    {
    case kControlConnectionChanged:
        OnConnectionChanged(wParam != 0);
        return 0;
    case kControlPing:
        m_controlChannel.Send(ipc::Pong{static_cast<std::uint32_t>(wParam)});
        return 0;
    case kControlMessages:
        OnControlMessages();
        return 0;
    case kOpenOverflowMessage:
        OpenOverflowMenu("UI Automation");
        return 0;
    case kWindowsChanged:
        OnWindowsChanged();
        return 0;
    case kRenderCommitted:
        OnCommitted(wParam);
        return 0;
    case kDeviceLostMessage:
        OnDeviceLost();
        return 0;
    case kCloakChangedMessage:
        UpdatePresentable();
        return 0;
    case TaskbarGuard::kPanicMessage:
        // Nothing of Harbor's may stay in the way: menus, the label, the edge triggers (until hiding is
        // turned on again).
        EndMenu();
        HideLabel();
        m_triggersOn = false;
        PlaceTriggers();
        return 0;
    case kTestShowLabelMessage:
        // wParam is a dock item (a slot), wherever it is on the island.
        if (const auto place = m_places.IndexOfSlot(wParam))
        {
            ShowLabel(*place);
        }
        return 0;
    case kTestHideLabelMessage:
        HideLabel();
        return 0;
    case kTestOpenMenuMessage:
        if (const auto place = m_places.IndexOfSlot(wParam))
        {
            OpenMenu(*place, "test hook");
        }
        return 0;
    case kTestCloseMenuMessage:
        EndMenu();
        return 0;
    case WM_MEASUREITEM:
        return adapters::PopupMenu::OnMeasureItem(*reinterpret_cast<MEASUREITEMSTRUCT*>(lParam))
                   ? TRUE
                   : DefWindowProcW(m_hwnd, message, wParam, lParam);
    case WM_DRAWITEM:
        return adapters::PopupMenu::OnDrawItem(*reinterpret_cast<const DRAWITEMSTRUCT*>(lParam))
                   ? TRUE
                   : DefWindowProcW(m_hwnd, message, wParam, lParam);
    case WM_MENUCHAR:
        if (const auto result = adapters::PopupMenu::OnMenuChar(wParam, lParam))
        {
            return *result;
        }
        return DefWindowProcW(m_hwnd, message, wParam, lParam);
    case WM_RBUTTONUP:
        OnRightClick(lParam);
        return 0;
    case kTestCloakMessage: {
        // Only the cloak hook notices: that is what the test checks.
        const BOOL cloak = wParam != 0;
        m_log.Warn(cloak ? "test hook: cloaking the island" : "test hook: uncloaking the island");
        DwmSetWindowAttribute(m_hwnd, DWMWA_CLOAK, &cloak, sizeof(cloak));
        return 0;
    }
    case kTestHangMessage:
        m_log.Warn("test hook: hanging the UI thread");
        Sleep(INFINITE);
        return 0;
    case kTestFullScreenMessage:
        m_log.Warn(wParam != 0 ? "test hook: full screen on" : "test hook: full screen off");
        if (!m_hosts.empty())
        {
            OnFullScreenNotice(DockHost(), wParam != 0);
        }
        return 0;
    case kTestBreakTriggerMessage:
        // Only the trigger goes: rule 7 must notice it by itself.
        m_log.Warn("test hook: breaking the dock's own trigger");
        m_testTriggerBroken = true;
        if (!m_hosts.empty())
        {
            ShowWindow(m_hosts[DockHost()].window, SW_HIDE);
        }
        return 0;
    case TaskbarGuard::kStateMessage:
        if (const bool hidden = wParam != 0; hidden != m_taskbarHidden)
        {
            // The tray button comes or goes (docs/adr/0012), before Harbor Shell hears of the change. Redrawn
            // in place: the island stays on screen.
            m_taskbarHidden = hidden;
            m_hovered.reset();
            HideLabel();
            Refresh(/*force=*/true);
            if (m_accessibility)
            {
                m_accessibility->ItemsChanged();
            }
        }
        m_controlChannel.Send(ipc::TaskbarState{wParam != 0, static_cast<ipc::TaskbarBlock>(lParam)});
        return 0;
    case Launcher::kLaunchedMessage:
        OnLaunched(reinterpret_cast<LaunchReport*>(lParam));
        return 0;
    case TaskbarGuard::kShowIslandMessage:
        // The panic hotkey hid the island; hiding the taskbar was turned on again: so are the edge triggers.
        if (m_shown)
        {
            m_log.Info("island: shown again");
            ShowWindow(m_hwnd, SW_SHOWNA);
        }
        m_triggersOn = true;
        PlaceTriggers();
        return 0;
    case WM_TIMER:
        if (wParam == kOrphanTimer)
        {
            m_log.Warn(std::format("test dock: no connection to Harbor Shell for {} ms; exiting",
                                   m_options.orphanTimeout.count()));
            KillTimer(m_hwnd, kOrphanTimer);
            RequestExit(0);
            return 0;
        }
        if (wParam == kDeviceRetryTimer)
        {
            KillTimer(m_hwnd, kDeviceRetryTimer);
            OnDeviceLost();
            return 0;
        }
        if (wParam == kEdgeTimer)
        {
            OnEdgeTimer();
            return 0;
        }
        if (wParam == kTuckTimer)
        {
            KillTimer(m_hwnd, kTuckTimer);
            m_tuck.OnTimer(SteadyNow());
            ApplyTuck();
            return 0;
        }
        if (wParam == kFullScreenTimer)
        {
            LookAgainAtFullScreen();
            return 0;
        }
        if (wParam == kIslandTimer)
        {
            KillTimer(m_hwnd, kIslandTimer);
            if (m_islandAskedAt && !m_island.open)
            {
                m_islandAskedAt.reset();
                m_log.Warn(std::format("island: not open {} ms after its hotkey; Windows Start instead",
                                       kIslandAnswerMs));
                OpenWindowsStart("the island didn't open");
            }
            return 0;
        }
        if (wParam == kLabelTimer)
        {
            KillTimer(m_hwnd, kLabelTimer);
            if (m_hovered)
            {
                ShowLabel(*m_hovered);
            }
            return 0;
        }
        if (wParam == kLabelReleaseTimer)
        {
            KillTimer(m_hwnd, kLabelReleaseTimer);
            m_renderer.ReleaseLabelResources();
            return 0;
        }
        if (wParam == kModelTimer)
        {
            KillTimer(m_hwnd, kModelTimer);
            RebuildModel(/*force=*/false);
            return 0;
        }
        return DefWindowProcW(m_hwnd, message, wParam, lParam);
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_MOUSEMOVE:
        OnMouseMove(lParam);
        return 0;
    case WM_MOUSELEAVE:
        m_trackingMouse = false;
        SetHovered(std::nullopt);
        // A menu's loop takes the mouse: the island stays while it is open.
        if (!m_menuOpen)
        {
            ReportPointer(PointerOverDock());
        }
        return 0;
    case WM_LBUTTONUP:
        OnClick(lParam);
        return 0;
    case WM_DPICHANGED:
        // Not Windows' suggested rectangle: the layout follows the dock's monitor, wherever this window is.
        Refresh(/*force=*/false);
        return 0;
    case kRecheckPresentable:
        UpdatePresentable(/*recheck=*/true);
        return 0;
    case WM_CLOSE:
        // Harbor Shell's "Stop dock", or anyone else: a normal exit.
        m_log.Info("asked to close; exiting");
        RequestExit(0);
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
        // Monitors or the work area changed: so may the island's place. A monitor that came gets a host, one
        // that went takes its host along, and the island goes back to the primary if its monitor went.
        if (message == WM_DISPLAYCHANGE || wParam == SPI_SETWORKAREA)
        {
            SyncHosts();
        }
        Refresh(/*force=*/false);
        UpdatePresentable();
        return DefWindowProcW(m_hwnd, message, wParam, lParam);
    case kWorkAreaMayHaveChanged:
        SyncHosts();
        Refresh(/*force=*/false);
        return 0;
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
        Refresh(/*force=*/false);
        return DefWindowProcW(m_hwnd, message, wParam, lParam);
    case WM_POWERBROADCAST:
        if (wParam == PBT_POWERSETTINGCHANGE)
        {
            if (const auto on =
                    adapters::EnergySaverIn(*reinterpret_cast<const POWERBROADCAST_SETTING*>(lParam));
                on && *on != m_energySaver)
            {
                m_energySaver = *on;
                m_log.Info(m_energySaver ? "island: energy saver on" : "island: energy saver off");
            }
            Refresh(/*force=*/false);
        }
        return TRUE;
    case WM_WINDOWPOSCHANGED:
        UpdatePresentable();
        return DefWindowProcW(m_hwnd, message, wParam, lParam);
    case WM_GETOBJECT:
        if (m_accessibility)
        {
            if (const auto result = m_accessibility->OnGetObject(wParam, lParam))
            {
                return *result;
            }
        }
        return DefWindowProcW(m_hwnd, message, wParam, lParam);
    case WM_DESTROY:
        m_cloakHook.reset();
        if (m_accessibility)
        {
            m_accessibility->Disconnect();
        }
        m_controlChannel.Stop();
        for (auto* notification : {&m_powerNotification, &m_energySaverNotification})
        {
            if (*notification != nullptr)
            {
                UnregisterPowerSettingNotification(*notification);
                *notification = nullptr;
            }
        }
        PostQuitMessage(m_exitCode);
        return 0;
    default:
        return DefWindowProcW(m_hwnd, message, wParam, lParam);
    }
}

} // namespace harbor::dock
