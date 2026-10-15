#include "app/dock_window.h"

#include <algorithm>
#include <dwmapi.h>
#include <format>
#include <iterator>
#include <memory>
#include <shellapi.h>
#include <ShellScalingApi.h>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "adapters/icon_files.h"
#include "adapters/system_appearance.h"
#include "adapters/win32_app_bar.h"
#include "app/exit_codes.h"
#include "core/island/icon_file.h"
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
constexpr UINT kControlDockItems = WM_APP + 4;         // the list is in m_receivedItems
// Posted by the renderer once a frame is on its way to the screen.
constexpr UINT kRenderCommitted = WM_APP + 5;
// Posted by the AppBar worker with the strip Explorer granted: wParam = left, top; lParam = right, bottom.
constexpr UINT kAppBarStrip = WM_APP + 8;
// Explorer's AppBar notifications (ABN_*): wParam = notification.
constexpr UINT kAppBarCallback = WM_APP + 9;

static_assert(sizeof(WPARAM) == 8 && sizeof(LPARAM) == 8, "two 32-bit coordinates go in each");

std::uint64_t PackPair(int high, int low) noexcept
{
    return static_cast<std::uint64_t>(static_cast<std::uint32_t>(high)) << 32 |
           static_cast<std::uint32_t>(low);
}

std::pair<int, int> UnpackPair(std::uint64_t packed) noexcept
{
    return {static_cast<int>(static_cast<std::uint32_t>(packed >> 32)),
            static_cast<int>(static_cast<std::uint32_t>(packed))};
}

constexpr UINT_PTR kOrphanTimer = 1;
constexpr UINT_PTR kDeviceRetryTimer = 2;
constexpr UINT kDeviceRetryMs = 2000;
// Failed attempts at a new graphics device before the dock gives up (and Harbor Shell restores the taskbar).
constexpr int kDeviceAttempts = 5;

struct MonitorFacts
{
    geometry::Rect monitor;
    geometry::Rect workArea;
    unsigned dpi = USER_DEFAULT_SCREEN_DPI;
};

geometry::Rect FromRect(const RECT& rect) noexcept
{
    return {rect.left, rect.top, rect.right, rect.bottom};
}

// The monitor the island is on: the primary until the dock follows the mouse (docs/adr/0011).
HMONITOR DockMonitor() noexcept
{
    return MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
}

MonitorFacts ReadMonitor(HMONITOR monitor) noexcept
{
    MonitorFacts result;
    MONITORINFO info{sizeof(info)};
    if (GetMonitorInfoW(monitor, &info))
    {
        result.monitor = FromRect(info.rcMonitor);
        result.workArea = FromRect(info.rcWork);
    }
    UINT dpiX = 0;
    UINT dpiY = 0;
    if (SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) && dpiX != 0)
    {
        result.dpi = dpiX;
    }
    return result;
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
    // The AppBar goes before the window does, waiting for Explorer a moment at most.
    if (m_appBar)
    {
        m_appBar->Stop();
        m_appBar.reset();
    }
    // A launch stuck in Explorer is left behind.
    m_launcher.reset();
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

    m_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
    // Energy saver turning on or off changes the island's look.
    m_powerNotification =
        RegisterPowerSettingNotification(m_hwnd, &GUID_POWER_SAVING_STATUS, DEVICE_NOTIFY_WINDOW_HANDLE);
    // A cloaked island isn't on screen (docs/adr/0011). Only this thread's windows: the hook costs nothing
    // while nothing here is cloaked.
    m_cloakHook.reset(SetWinEventHook(EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED, nullptr, &OnCloakChanged,
                                      GetCurrentProcessId(), GetCurrentThreadId(), WINEVENT_OUTOFCONTEXT));
    if (!m_cloakHook)
    {
        m_log.Warn(std::format("island: no cloak notifications ({})", GetLastError()));
    }

    // Test docks reserve nothing: tests never change the user's work area.
    std::unique_ptr<appbar::IAppBar> appBar;
    if (m_options.testAppBar)
    {
        appBar = std::make_unique<adapters::TestAppBar>();
    }
    else
    {
        appBar = std::make_unique<adapters::Win32AppBar>(m_hwnd, kAppBarCallback);
    }
    const HWND hwnd = m_hwnd;
    m_appBar.emplace(std::move(appBar), [hwnd](const geometry::Rect& strip) {
        PostMessageW(hwnd, kAppBarStrip, PackPair(strip.left, strip.top),
                     static_cast<LPARAM>(PackPair(strip.right, strip.bottom)));
    });
    Refresh(/*force=*/true);
    m_launcher.emplace(m_hwnd);
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
    LatestValue<ipc::DockItems>& receivedItems = m_receivedItems;
    m_controlChannel.Start({
        .onMessage =
            [hwnd, &guard, &receivedItems](const ipc::Message& message) {
                if (const auto* ping = std::get_if<ipc::Ping>(&message))
                {
                    PostMessageW(hwnd, kControlPing, ping->seq, 0);
                    guard.PostPing();
                }
                else if (const auto* setTaskbar = std::get_if<ipc::SetTaskbar>(&message))
                {
                    guard.PostRequest(setTaskbar->hidden);
                }
                else if (const auto* dockItems = std::get_if<ipc::DockItems>(&message))
                {
                    receivedItems.Put(*dockItems);
                    PostMessageW(hwnd, kControlDockItems, 0, 0);
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
}

void DockWindow::OnDockItems()
{
    // Several lists may have arrived for one wake-up; the newest wins.
    if (auto items = m_receivedItems.Take(); items && *items != m_items)
    {
        m_items = std::move(*items);
        AssignItemKeys();
        // Only the count: the names are the user's business.
        m_log.Info(std::format("dock items: {} received", m_items.items.size()));
        m_hovered.reset();
        Refresh(/*force=*/true);
        if (m_accessibility)
        {
            // Names and order may differ: UI Automation clients must look again.
            m_accessibility->ItemsChanged();
        }
    }
}

void DockWindow::Refresh(bool force)
{
    if (m_deviceLost)
    {
        // Nothing can be drawn now; getting a new device redraws everything.
        return;
    }
    const auto monitor = ReadMonitor(DockMonitor());
    const auto style = island::StyleFor(adapters::ReadSystemAppearance());
    const auto metrics = island::MeasureFor(monitor.monitor.Width(), monitor.dpi, m_items.items.size());
    // The AppBar asks Explorer for the strip (and it answers on kAppBarStrip); same request, nothing happens.
    if (m_appBar)
    {
        m_appBar->SetWant({monitor.monitor, metrics.StripHeight()});
    }
    // Until Explorer answers (or if it never does): the bottom of the work area, above a visible taskbar.
    // That is usually where the strip goes too.
    const geometry::Rect strip =
        m_strip.value_or(geometry::Rect{monitor.monitor.left, monitor.workArea.bottom - metrics.StripHeight(),
                                        monitor.monitor.right, monitor.workArea.bottom});
    auto layout = island::Arrange(strip, metrics);

    if (!force && m_layout && layout.island == m_layout->island && layout.items == m_layout->items &&
        style == m_style && monitor.dpi == m_dpi)
    {
        return;
    }
    if (metrics.visibleCount < m_items.items.size())
    {
        m_log.Warn(std::format("island: {} of {} items fit on the dock's monitor", metrics.visibleCount,
                               m_items.items.size()));
    }

    const auto& island = layout.island;
    // Behind a full-screen app, above everything else.
    SetWindowPos(m_hwnd, m_fullScreen ? HWND_BOTTOM : HWND_TOPMOST, island.left, island.top, island.Width(),
                 island.Height(), SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    // Clicks on the transparent corners go to the window below.
    const int diameter = 2 * metrics.cornerRadius;
    SetWindowRgn(m_hwnd,
                 CreateRoundRectRgn(0, 0, island.Width() + 1, island.Height() + 1, diameter, diameter), TRUE);

    m_style = style;
    m_dpi = monitor.dpi;
    const bool countChanged = !m_layout || m_layout->items.size() != layout.items.size();
    m_layout = std::move(layout);
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
    const auto& items = m_items.items;
    const auto& iconsDir = m_options.iconsDir;
    const adapters::CompositionRenderer::IconSource icons =
        [&](std::size_t index, std::vector<std::uint8_t>& file) -> std::optional<island::IconView> {
        const auto& icon = items[index].icon;
        file = icon ? adapters::ReadIconFile(iconsDir, *icon) : std::vector<std::uint8_t>{};
        auto view = island::ParseIconFile(file);
        drawn += view.has_value() ? 1 : 0;
        return view;
    };

    if (m_renderer.Render(*m_layout, m_style, icons, {}, m_hovered, kRenderCommitted, ++m_renderGeneration))
    {
        m_log.Info(std::format("island: {} items ({} icons), {}x{} at {},{} ({} dpi, {})",
                               m_layout->items.size(), drawn, island.Width(), island.Height(), island.left,
                               island.top, m_dpi, m_style.backdrop ? "backdrop" : "solid"));
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

void DockWindow::UpdatePresentable()
{
    // On screen: drawn, visible, not cloaked, and inside one connected monitor (docs/adr/0011).
    island::WindowFacts facts{.committed = m_committed && m_layout.has_value(),
                              .visible = IsWindowVisible(m_hwnd) != FALSE};
    DWORD cloaked = 0;
    facts.cloaked =
        SUCCEEDED(DwmGetWindowAttribute(m_hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0;
    if (RECT window{}; GetWindowRect(m_hwnd, &window))
    {
        facts.window = FromRect(window);
    }
    const bool presentable = island::IsOnScreen(facts, ConnectedMonitors());
    if (presentable != m_presentable)
    {
        m_presentable = presentable;
        m_log.Info(presentable ? "island: shown" : "island: not shown");
        // The taskbar may only be hidden while the island is on screen.
        m_guard.PostPresentable(presentable);
    }
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
    if (hovered != m_hovered)
    {
        m_hovered = hovered;
        m_renderer.SetHovered(hovered);
    }
}

void DockWindow::OnMouseMove(LPARAM position)
{
    if (!m_trackingMouse)
    {
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, m_hwnd, 0};
        m_trackingMouse = TrackMouseEvent(&track) != FALSE;
    }
    SetHovered(ItemAtClient(position));
}

void DockWindow::OnClick(LPARAM position)
{
    if (const auto item = ItemAtClient(position))
    {
        LaunchItem(*item, "click");
    }
}

bool DockWindow::LaunchItem(std::size_t index, std::string_view how)
{
    if (!m_layout || index >= m_layout->items.size() || index >= m_items.items.size() || !m_launcher)
    {
        return false;
    }
    // The user asked: whatever starts may take the foreground (Explorer starts it, not the dock).
    AllowSetForegroundWindow(ASFW_ANY);
    // Which item, not which app: the pinned list is the user's business.
    m_log.Info(std::format("launch: item {} ({})", index, how));
    m_launcher->Launch(index, m_items.items[index]);
    return true;
}

void DockWindow::AssignItemKeys()
{
    std::map<std::string, std::uint64_t> keys;
    m_itemKeys.clear();
    for (const auto& item : m_items.items)
    {
        // The same pin twice is two items: each later one's identity gets another line ending.
        auto identity = std::format("{}\n{}\n", static_cast<int>(item.kind), item.target);
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
    return index < m_itemKeys.size() ? m_itemKeys[index] : 0;
}

std::optional<std::size_t> DockWindow::IndexOfKey(std::uint64_t key) const
{
    const auto visible = std::min(ItemCount(), m_itemKeys.size());
    for (std::size_t i = 0; i < visible; ++i)
    {
        if (m_itemKeys[i] == key)
        {
            return i;
        }
    }
    return std::nullopt;
}

std::wstring DockWindow::ItemName(std::size_t index) const
{
    if (index >= m_items.items.size())
    {
        return {};
    }
    const auto& name = m_items.items[index].name;
    const int length =
        MultiByteToWideChar(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(std::max(length, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), wide.data(), length);
    return wide;
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
    return LaunchItem(index, "UI Automation");
}

void DockWindow::OnAppBarNotification(WPARAM notification, LPARAM lParam)
{
    switch (notification)
    {
    case ABN_POSCHANGED:
        // Explorer sends these in bursts; the worker coalesces them.
        if (m_appBar)
        {
            m_appBar->PositionMayHaveChanged();
        }
        break;
    case ABN_FULLSCREENAPP:
        // A full-screen app opened (or closed) on this monitor: step behind it, like the taskbar does. The
        // island stays on screen in GuardPolicy's sense, as the hidden taskbar would have been covered too.
        if (m_fullScreen != (lParam != 0))
        {
            m_fullScreen = lParam != 0;
            m_log.Info(m_fullScreen ? "appbar: a full-screen app opened; the island steps behind it"
                                    : "appbar: the full-screen app closed; the island is on top again");
            SetWindowPos(m_hwnd, m_fullScreen ? HWND_BOTTOM : HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
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

void DockWindow::OnLaunched(LaunchReport* report)
{
    const std::unique_ptr<LaunchReport> owned(report);
    const auto& result = owned->result;
    if (FAILED(result.result))
    {
        m_log.Warn(std::format("launch: item {} failed: 0x{:08X} (Explorer: 0x{:08X})", owned->index,
                               static_cast<std::uint32_t>(result.result),
                               static_cast<std::uint32_t>(result.explorerResult)));
    }
    else if (result.route == adapters::LaunchRoute::Explorer)
    {
        m_log.Info(std::format("launch: item {} started via Explorer", owned->index));
    }
    else
    {
        m_log.Info(std::format("launch: item {} started via ShellExecuteEx (Explorer: 0x{:08X})",
                               owned->index, static_cast<std::uint32_t>(result.explorerResult)));
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
        if (m_appBar)
        {
            m_appBar->TaskbarCreated();
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
    case kControlDockItems:
        OnDockItems();
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
    case TaskbarGuard::kStateMessage:
        m_controlChannel.Send(ipc::TaskbarState{wParam != 0, static_cast<ipc::TaskbarBlock>(lParam)});
        return 0;
    case Launcher::kLaunchedMessage:
        OnLaunched(reinterpret_cast<LaunchReport*>(lParam));
        return 0;
    case TaskbarGuard::kShowIslandMessage:
        // The panic hotkey hid the island; hiding the taskbar was turned on again.
        if (m_shown)
        {
            m_log.Info("island: shown again");
            ShowWindow(m_hwnd, SW_SHOWNA);
        }
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
        return DefWindowProcW(m_hwnd, message, wParam, lParam);
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_MOUSEMOVE:
        OnMouseMove(lParam);
        return 0;
    case WM_MOUSELEAVE:
        m_trackingMouse = false;
        SetHovered(std::nullopt);
        return 0;
    case WM_LBUTTONUP:
        OnClick(lParam);
        return 0;
    case WM_DPICHANGED:
        // Not Windows' suggested rectangle: the layout follows the dock's monitor, wherever this window is.
        Refresh(/*force=*/false);
        return 0;
    case kAppBarStrip: {
        const auto [left, top] = UnpackPair(wParam);
        const auto [right, bottom] = UnpackPair(static_cast<std::uint64_t>(lParam));
        m_strip = geometry::Rect{left, top, right, bottom};
        m_log.Info(std::format("appbar: strip {},{} to {},{}", left, top, right, bottom));
        Refresh(/*force=*/false);
        return 0;
    }
    case kAppBarCallback:
        OnAppBarNotification(wParam, lParam);
        return 0;
    case WM_CLOSE:
        // Harbor Shell's "Stop dock", or anyone else: a normal exit.
        m_log.Info("asked to close; exiting");
        RequestExit(0);
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
        // Monitors or the work area changed: so may the strip.
        if (m_appBar && (message == WM_DISPLAYCHANGE || wParam == SPI_SETWORKAREA))
        {
            m_appBar->PositionMayHaveChanged();
        }
        Refresh(/*force=*/false);
        return DefWindowProcW(m_hwnd, message, wParam, lParam);
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
        Refresh(/*force=*/false);
        return DefWindowProcW(m_hwnd, message, wParam, lParam);
    case WM_POWERBROADCAST:
        if (wParam == PBT_POWERSETTINGCHANGE)
        {
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
        if (m_powerNotification != nullptr)
        {
            UnregisterPowerSettingNotification(m_powerNotification);
            m_powerNotification = nullptr;
        }
        PostQuitMessage(m_exitCode);
        return 0;
    default:
        return DefWindowProcW(m_hwnd, message, wParam, lParam);
    }
}

} // namespace harbor::dock
