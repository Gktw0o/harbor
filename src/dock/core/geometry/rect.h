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
    // inner lies entirely inside this rectangle. An empty inner never does.
    bool Contains(const Rect& inner) const noexcept
    {
        return inner.left < inner.right && inner.top < inner.bottom && inner.left >= left &&
               inner.top >= top && inner.right <= right && inner.bottom <= bottom;
    }
    friend bool operator==(const Rect&, const Rect&) = default;
};

} // namespace harbor::geometry
