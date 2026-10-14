#include "app/dock_window.h"

#include <string>
#include <variant>

namespace harbor::dock
{
namespace
{

constexpr wchar_t kWindowClass[] = L"Harbor.Dock.Window";
constexpr DWORD kWindowStyle = WS_OVERLAPPEDWINDOW;
constexpr int kWidthDip = 480;
constexpr int kHeightDip = 120;

// Posted by the control channel's thread; handled on the UI thread.
constexpr UINT kControlConnectionChanged = WM_APP + 1; // wParam: connected (0/1)
constexpr UINT kControlPing = WM_APP + 2;              // wParam: seq

} // namespace

DockWindow::DockWindow(ipc::IIpcChannel& controlChannel) : m_controlChannel(controlChannel)
{
}

bool DockWindow::Create(HINSTANCE instance, int showCommand)
{
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &DockWindow::WndProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kWindowClass;
    if (RegisterClassExW(&windowClass) == 0)
    {
        return false;
    }

    // Size is unknown until the window lands on a monitor, so create it empty and size it for that DPI.
    if (CreateWindowExW(0, kWindowClass, L"", kWindowStyle, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, nullptr,
                        nullptr, instance, this) == nullptr)
    {
        return false;
    }

    const UINT dpi = GetDpiForWindow(m_hwnd);
    RECT bounds{0, 0, MulDiv(kWidthDip, dpi, USER_DEFAULT_SCREEN_DPI),
                MulDiv(kHeightDip, dpi, USER_DEFAULT_SCREEN_DPI)};
    AdjustWindowRectExForDpi(&bounds, kWindowStyle, FALSE, 0, dpi);
    SetWindowPos(m_hwnd, nullptr, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    UpdateTitle();
    ShowWindow(m_hwnd, showCommand);
    StartControlChannel();
    return true;
}

void DockWindow::StartControlChannel()
{
    // Callbacks run on the channel's thread, so they only post to the window. PostMessage to a window that
    // has already been destroyed fails harmlessly.
    const HWND hwnd = m_hwnd;
    m_controlChannel.Start({
        .onMessage =
            [hwnd](const ipc::Message& message) {
                if (const auto* ping = std::get_if<ipc::Ping>(&message))
                {
                    PostMessageW(hwnd, kControlPing, ping->seq, 0);
                }
            },
        .onConnectionChanged =
            [hwnd](bool connected) { PostMessageW(hwnd, kControlConnectionChanged, connected, 0); },
        .onDiagnostic =
            [](std::string_view text) {
                std::string line = "harbor-dock: control channel: ";
                line.append(text);
                line.push_back('\n');
                OutputDebugStringA(line.c_str());
            },
    });
}

void DockWindow::OnPing(std::uint32_t seq)
{
    m_controlChannel.Send(ipc::Pong{seq});
    m_hasPing = true;
    m_lastPingSeq = seq;
    UpdateTitle();
}

void DockWindow::UpdateTitle()
{
    std::wstring title = L"Harbor Dock - ";
    if (!m_connected)
    {
        title += L"waiting for Harbor Shell";
    }
    else if (!m_hasPing)
    {
        title += L"connected";
    }
    else
    {
        title += L"connected, ping " + std::to_wstring(m_lastPingSeq);
    }
    SetWindowTextW(m_hwnd, title.c_str());
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
    switch (message)
    {
    case kControlConnectionChanged:
        m_connected = wParam != 0;
        m_hasPing = false;
        UpdateTitle();
        return 0;
    case kControlPing:
        OnPing(static_cast<std::uint32_t>(wParam));
        return 0;
    case WM_DPICHANGED: {
        // Windows suggests a rectangle that keeps the window's DIP size on the new monitor.
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(m_hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_DESTROY:
        m_controlChannel.Stop();
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(m_hwnd, message, wParam, lParam);
    }
}

} // namespace harbor::dock
