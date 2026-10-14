#include "adapters/win32_app_bar.h"

#include <shellapi.h>

namespace harbor::adapters
{
namespace
{

RECT ToRect(const geometry::Rect& rect) noexcept
{
    return {rect.left, rect.top, rect.right, rect.bottom};
}

geometry::Rect FromRect(const RECT& rect) noexcept
{
    return {rect.left, rect.top, rect.right, rect.bottom};
}

} // namespace

Win32AppBar::Win32AppBar(HWND window, UINT callbackMessage) noexcept
    : m_window(window), m_callbackMessage(callbackMessage)
{
}

bool Win32AppBar::Register()
{
    APPBARDATA data{sizeof(data)};
    data.hWnd = m_window;
    data.uCallbackMessage = m_callbackMessage;
    return SHAppBarMessage(ABM_NEW, &data) != FALSE;
}

void Win32AppBar::Remove()
{
    APPBARDATA data{sizeof(data)};
    data.hWnd = m_window;
    SHAppBarMessage(ABM_REMOVE, &data);
}

geometry::Rect Win32AppBar::Query(const geometry::Rect& proposed)
{
    APPBARDATA data{sizeof(data)};
    data.hWnd = m_window;
    data.uEdge = ABE_BOTTOM;
    data.rc = ToRect(proposed);
    SHAppBarMessage(ABM_QUERYPOS, &data);
    return FromRect(data.rc);
}

geometry::Rect Win32AppBar::Set(const geometry::Rect& proposed)
{
    APPBARDATA data{sizeof(data)};
    data.hWnd = m_window;
    data.uEdge = ABE_BOTTOM;
    data.rc = ToRect(proposed);
    SHAppBarMessage(ABM_SETPOS, &data);
    return FromRect(data.rc);
}

bool TestAppBar::Register()
{
    return true;
}

void TestAppBar::Remove()
{
}

geometry::Rect TestAppBar::Query(const geometry::Rect& proposed)
{
    MONITORINFO info{sizeof(info)};
    if (!GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &info))
    {
        return proposed;
    }
    auto granted = proposed;
    if (granted.bottom > info.rcWork.bottom)
    {
        granted.top -= granted.bottom - info.rcWork.bottom;
        granted.bottom = info.rcWork.bottom;
    }
    return granted;
}

geometry::Rect TestAppBar::Set(const geometry::Rect& proposed)
{
    return Query(proposed);
}

} // namespace harbor::adapters
