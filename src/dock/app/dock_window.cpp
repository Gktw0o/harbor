#include "app/dock_window.h"

#include <algorithm>
#include <format>
#include <memory>
#include <ShellScalingApi.h>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "adapters/icon_files.h"
#include "adapters/system_appearance.h"
#include "app/exit_codes.h"
#include "core/island/icon_file.h"

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

constexpr UINT_PTR kOrphanTimer = 1;
constexpr UINT_PTR kDeviceRetryTimer = 2;
constexpr UINT kDeviceRetryMs = 2000;
// Failed attempts at a new graphics device before the dock gives up (and Harbor Shell restores the taskbar).
constexpr int kDeviceAttempts = 5;

struct PrimaryMonitor
{
    geometry::Rect monitor;
    geometry::Rect workArea;
    unsigned dpi = USER_DEFAULT_SCREEN_DPI;
};

geometry::Rect FromRect(const RECT& rect) noexcept
{
    return {rect.left, rect.top, rect.right, rect.bottom};
}

PrimaryMonitor ReadPrimaryMonitor() noexcept
{
    PrimaryMonitor result;
    const HMONITOR monitor = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
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

} // namespace

DockWindow::DockWindow(ipc::IIpcChannel& controlChannel, TaskbarGuard& guard,
                       adapters::CompositionRenderer& renderer, log::ILogger& log, Options options)
    : m_controlChannel(controlChannel), m_guard(guard), m_renderer(renderer), m_log(log), m_options(options)
{
}

DockWindow::~DockWindow()
{
    // Normally stopped by WM_DESTROY already; its callbacks use this object.
    m_controlChannel.Stop();
    // Here rather than at WM_DESTROY: by now the taskbar is back (main stops the guard first), so a launch
    // stuck in Explorer can't delay that.
    m_launcher.reset();
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
    Refresh(/*force=*/true);
    m_launcher.emplace(m_hwnd);

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
        // Only the count: the names are the user's business.
        m_log.Info(std::format("dock items: {} received", m_items.items.size()));
        m_hovered.reset();
        Refresh(/*force=*/true);
    }
}

void DockWindow::Refresh(bool force)
{
    if (m_deviceLost)
    {
        // Nothing can be drawn now; getting a new device redraws everything.
        return;
    }
    const auto primary = ReadPrimaryMonitor();
    const auto style = island::StyleFor(adapters::ReadSystemAppearance());
    const auto metrics = island::MeasureFor(primary.monitor.Width(), primary.dpi, m_items.items.size());
    // Until the AppBar reserves a strip: the bottom of the work area, above a visible taskbar.
    const geometry::Rect strip{primary.monitor.left, primary.workArea.bottom - metrics.StripHeight(),
                               primary.monitor.right, primary.workArea.bottom};
    auto layout = island::Arrange(strip, metrics);

    if (!force && m_layout && layout.island == m_layout->island && layout.items == m_layout->items &&
        style == m_style && primary.dpi == m_dpi)
    {
        return;
    }
    if (metrics.visibleCount < m_items.items.size())
    {
        m_log.Warn(std::format("island: {} of {} items fit on the primary monitor", metrics.visibleCount,
                               m_items.items.size()));
    }

    const auto& island = layout.island;
    SetWindowPos(m_hwnd, HWND_TOPMOST, island.left, island.top, island.Width(), island.Height(),
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    // Clicks on the transparent corners go to the window below.
    const int diameter = 2 * metrics.cornerRadius;
    SetWindowRgn(m_hwnd,
                 CreateRoundRectRgn(0, 0, island.Width() + 1, island.Height() + 1, diameter, diameter), TRUE);

    m_style = style;
    m_dpi = primary.dpi;
    m_layout = std::move(layout);
    if (m_hovered && *m_hovered >= m_layout->items.size())
    {
        m_hovered.reset();
    }
    // Read again on every redraw rather than kept: decoded, 40 icons would hold 10 MB (rule 4).
    std::vector<std::vector<std::uint8_t>> files;
    std::vector<std::optional<island::IconView>> icons;
    files.reserve(m_layout->items.size());
    for (std::size_t i = 0; i < m_layout->items.size(); ++i)
    {
        const auto& icon = m_items.items[i].icon;
        files.push_back(icon ? adapters::ReadIconFile(m_options.iconsDir, *icon)
                             : std::vector<std::uint8_t>{});
        icons.push_back(island::ParseIconFile(files.back()));
    }
    const auto drawn =
        std::count_if(icons.begin(), icons.end(), [](const auto& icon) { return icon.has_value(); });

    if (m_renderer.Render(*m_layout, m_style, icons, m_hovered, kRenderCommitted, ++m_renderGeneration))
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
            m_exitCode = exit_code::kGraphicsUnavailable;
            DestroyWindow(m_hwnd);
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
    // On screen: drawn, visible, and on the primary monitor.
    bool presentable = m_committed && m_layout && IsWindowVisible(m_hwnd);
    if (presentable)
    {
        RECT window{};
        RECT visible{};
        const auto primary = ReadPrimaryMonitor();
        const RECT monitor{primary.monitor.left, primary.monitor.top, primary.monitor.right,
                           primary.monitor.bottom};
        presentable = GetWindowRect(m_hwnd, &window) && IntersectRect(&visible, &window, &monitor) &&
                      EqualRect(&visible, &window);
    }
    if (presentable != m_presentable)
    {
        m_presentable = presentable;
        m_log.Info(presentable ? "island: shown" : "island: not shown");
        // The taskbar may only be hidden while the island is on screen.
        m_guard.PostPresentable(presentable);
    }
}

std::optional<std::size_t> DockWindow::ItemAt(LPARAM position) const
{
    if (!m_layout)
    {
        return std::nullopt;
    }
    // Client coordinates; the layout is in screen coordinates.
    const geometry::Point point{m_layout->island.left + static_cast<short>(LOWORD(position)),
                                m_layout->island.top + static_cast<short>(HIWORD(position))};
    return island::HitTest(*m_layout, point);
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
    SetHovered(ItemAt(position));
}

void DockWindow::OnClick(LPARAM position)
{
    const auto item = ItemAt(position);
    if (!item || *item >= m_items.items.size() || !m_launcher)
    {
        return;
    }
    // The click was the user's: whatever starts may take the foreground (Explorer starts it, not the dock).
    AllowSetForegroundWindow(ASFW_ANY);
    // Which item, not which app: the pinned list is the user's business.
    m_log.Info(std::format("launch: item {}", *item));
    m_launcher->Launch(*item, m_items.items[*item]);
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
        // Explorer restarted, or the primary monitor's DPI changed.
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
            DestroyWindow(m_hwnd);
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
        // Not Windows' suggested rectangle: the layout follows the primary monitor, wherever this window is.
        Refresh(/*force=*/false);
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
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
    case WM_DESTROY:
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
