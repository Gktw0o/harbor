#include "app/dock_window.h"

#include <format>
#include <ShellScalingApi.h>
#include <string>
#include <utility>
#include <variant>

#include "adapters/system_appearance.h"

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
    if (!m_renderer.Attach(m_hwnd))
    {
        return CreateResult::GraphicsFailed;
    }

    m_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
    // Energy saver turning on or off changes the island's look.
    m_powerNotification =
        RegisterPowerSettingNotification(m_hwnd, &GUID_POWER_SAVING_STATUS, DEVICE_NOTIFY_WINDOW_HANDLE);
    Refresh(/*force=*/true);

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
    if (m_renderer.Render(*m_layout, m_style, m_hovered, kRenderCommitted))
    {
        m_log.Info(std::format("island: {} items, {}x{} at {},{} ({} dpi, {})", m_layout->items.size(),
                               island.Width(), island.Height(), island.left, island.top, m_dpi,
                               m_style.backdrop ? "backdrop" : "solid"));
    }
    UpdatePresentable();
}

void DockWindow::OnCommitted()
{
    if (!m_committed)
    {
        m_committed = true;
        // Nothing was visible before the first frame: no flash of an empty window.
        ShowWindow(m_hwnd, SW_SHOWNA);
    }
    UpdatePresentable();
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
    if (const auto item = ItemAt(position))
    {
        // Launching comes with the shell threads.
        m_log.Info(std::format("island: item {} clicked", *item));
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
        OnCommitted();
        return 0;
    case kTestHangMessage:
        m_log.Warn("test hook: hanging the UI thread");
        Sleep(INFINITE);
        return 0;
    case TaskbarGuard::kStateMessage:
        m_controlChannel.Send(ipc::TaskbarState{wParam != 0, static_cast<ipc::TaskbarBlock>(lParam)});
        return 0;
    case WM_TIMER:
        if (wParam == kOrphanTimer)
        {
            m_log.Warn(std::format("test dock: no connection to Harbor Shell for {} ms; exiting",
                                   m_options.orphanTimeout.count()));
            DestroyWindow(m_hwnd);
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
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(m_hwnd, message, wParam, lParam);
    }
}

} // namespace harbor::dock
