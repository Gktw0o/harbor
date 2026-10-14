#include "app/dock_window.h"

namespace harbor::dock
{
namespace
{

constexpr wchar_t kWindowClass[] = L"Harbor.Dock.Window";
constexpr wchar_t kWindowTitle[] = L"Harbor Dock";
constexpr DWORD kWindowStyle = WS_OVERLAPPEDWINDOW;
constexpr int kWidthDip = 480;
constexpr int kHeightDip = 120;

} // namespace

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
    if (CreateWindowExW(0, kWindowClass, kWindowTitle, kWindowStyle, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0,
                        nullptr, nullptr, instance, this) == nullptr)
    {
        return false;
    }

    const UINT dpi = GetDpiForWindow(m_hwnd);
    RECT bounds{0, 0, MulDiv(kWidthDip, dpi, USER_DEFAULT_SCREEN_DPI),
                MulDiv(kHeightDip, dpi, USER_DEFAULT_SCREEN_DPI)};
    AdjustWindowRectExForDpi(&bounds, kWindowStyle, FALSE, 0, dpi);
    SetWindowPos(m_hwnd, nullptr, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    ShowWindow(m_hwnd, showCommand);
    return true;
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
    case WM_DPICHANGED: {
        // Windows suggests a rectangle that keeps the window's DIP size on the new monitor.
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(m_hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(m_hwnd, message, wParam, lParam);
    }
}

} // namespace harbor::dock
