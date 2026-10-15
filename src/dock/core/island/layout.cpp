#include "core/island/layout.h"

#include <algorithm>

namespace harbor::island
{

int Scale(int dip, unsigned dpi) noexcept
{
    return static_cast<int>((static_cast<long long>(dip) * dpi + 48) / 96);
}

Metrics MeasureFor(int monitorWidth, unsigned dpi, std::size_t itemCount, bool divider) noexcept
{
    Metrics metrics{
        .icon = Scale(kIconDip, dpi),
        .padding = Scale(kPaddingDip, dpi),
        .gap = Scale(kGapDip, dpi),
        .margin = Scale(kMarginDip, dpi),
        .divider = divider ? Scale(kDividerDip, dpi) : 0,
    };
    const int minIcon = Scale(kMinIconDip, dpi);
    // Room for the icons and the gaps between them (one of which is the divider, if any).
    const int dividerExtra = divider ? metrics.divider - metrics.gap : 0;
    const long long room =
        std::max(0, monitorWidth - 2 * metrics.margin - 2 * metrics.padding - dividerExtra);
    // An empty dock still has room for one slot.
    const long long slots = static_cast<long long>(std::max<std::size_t>(itemCount, 1));

    const long long needed = slots * metrics.icon + (slots - 1) * metrics.gap;
    if (needed > room)
    {
        const long long fitted = (room - (slots - 1) * metrics.gap) / slots;
        metrics.icon = static_cast<int>(std::clamp<long long>(fitted, minIcon, metrics.icon));
    }
    const long long fitting = (room + metrics.gap) / (metrics.icon + metrics.gap);
    metrics.visibleCount =
        static_cast<std::size_t>(std::min<long long>(fitting, static_cast<long long>(itemCount)));
    metrics.cornerRadius = std::min(Scale(kCornerRadiusDip, dpi), (2 * metrics.padding + metrics.icon) / 2);
    return metrics;
}

Layout Arrange(const geometry::Rect& strip, const Metrics& metrics, std::size_t dividerAfter)
{
    const int slots = static_cast<int>(std::max<std::size_t>(metrics.visibleCount, 1));
    const bool divider = metrics.divider > 0 && dividerAfter > 0 && dividerAfter < metrics.visibleCount;
    const int dividerExtra = divider ? metrics.divider - metrics.gap : 0;
    const int width = 2 * metrics.padding + slots * metrics.icon + (slots - 1) * metrics.gap + dividerExtra;
    const int height = 2 * metrics.padding + metrics.icon;

    Layout layout{.metrics = metrics};
    layout.island.left = strip.left + (strip.Width() - width) / 2;
    layout.island.right = layout.island.left + width;
    layout.island.bottom = strip.bottom - metrics.margin;
    layout.island.top = layout.island.bottom - height;

    const int top = layout.island.top + metrics.padding;
    int left = layout.island.left + metrics.padding;
    for (std::size_t i = 0; i < metrics.visibleCount; ++i)
    {
        if (divider && i == dividerAfter)
        {
            // A thin line in the middle of the divider's room, a little shorter than the icons.
            const int line = std::max(1, metrics.padding / 8);
            const int x = left - metrics.gap + metrics.divider / 2 - line / 2;
            const int inset = metrics.icon / 5;
            layout.divider = geometry::Rect{x, top + inset, x + line, top + metrics.icon - inset};
            left += dividerExtra;
        }
        layout.items.push_back({left, top, left + metrics.icon, top + metrics.icon});
        left += metrics.icon + metrics.gap;
    }
    return layout;
}

std::optional<std::size_t> HitTest(const Layout& layout, geometry::Point point) noexcept
{
    if (layout.items.empty() || !layout.island.Contains(point))
    {
        return std::nullopt;
    }
    // Neighbours split the space between them down the middle; the first and last reach the island's edges.
    for (std::size_t i = 0; i + 1 < layout.items.size(); ++i)
    {
        if (point.x < (layout.items[i].right + layout.items[i + 1].left) / 2)
        {
            return i;
        }
    }
    return layout.items.size() - 1;
}

geometry::Rect DotFor(const Layout& layout, std::size_t index) noexcept
{
    const auto& item = layout.items[index];
    const int padding = layout.metrics.padding;
    const int diameter = std::max(2, padding / 2);
    const int left = item.left + (item.Width() - diameter) / 2;
    const int top = item.bottom + (padding - diameter) / 2;
    return {left, top, left + diameter, top + diameter};
}

} // namespace harbor::island
