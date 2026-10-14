#pragma once

namespace harbor::geometry
{

struct Point
{
    int x = 0;
    int y = 0;
    friend bool operator==(const Point&, const Point&) = default;
};

// Screen rectangle in physical pixels, right and bottom exclusive (like Win32's RECT).
struct Rect
{
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    int Width() const noexcept
    {
        return right - left;
    }
    int Height() const noexcept
    {
        return bottom - top;
    }
    bool Contains(Point point) const noexcept
    {
        return point.x >= left && point.x < right && point.y >= top && point.y < bottom;
    }
    friend bool operator==(const Rect&, const Rect&) = default;
};

} // namespace harbor::geometry
