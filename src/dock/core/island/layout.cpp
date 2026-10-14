#include "core/island/layout.h"

#include <algorithm>

namespace harbor::island
{

int Scale(int dip, unsigned dpi) noexcept
{
    return static_cast<int>((static_cast<long long>(dip) * dpi + 48) / 96);
}

Metrics MeasureFor(int monitorWidth, unsigned dpi, std::size_t itemCount) noexcept
{
    Metrics metrics{
        .icon = Scale(kIconDip, dpi),
        .padding = Scale(kPaddingDip, dpi),
        .gap = Scale(kGapDip, dpi),
        .margin = Scale(kMarginDip, dpi),
    };
    const int minIcon = Scale(kMinIconDip, dpi);
    // Room for the icons and the gaps between them.
    const long long room = std::max(0, monitorWidth - 2 * metrics.margin - 2 * metrics.padding);
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

Layout Arrange(const geometry::Rect& strip, const Metrics& metrics)
{
    const int slots = static_cast<int>(std::max<std::size_t>(metrics.visibleCount, 1));
    const int width = 2 * metrics.padding + slots * metrics.icon + (slots - 1) * metrics.gap;
    const int height = 2 * metrics.padding + metrics.icon;

    Layout layout{.metrics = metrics};
    layout.island.left = strip.left + (strip.Width() - width) / 2;
    layout.island.right = layout.island.left + width;
    layout.island.bottom = strip.bottom - metrics.margin;
    layout.island.top = layout.island.bottom - height;

    for (int i = 0; i < static_cast<int>(metrics.visibleCount); ++i)
    {
        const int left = layout.island.left + metrics.padding + i * (metrics.icon + metrics.gap);
        const int top = layout.island.top + metrics.padding;
        layout.items.push_back({left, top, left + metrics.icon, top + metrics.icon});
    }
    return layout;
}

std::optional<std::size_t> HitTest(const Layout& layout, geometry::Point point) noexcept
{
    if (layout.items.empty() || !layout.island.Contains(point))
    {
        return std::nullopt;
    }
    // Slots split the gaps down the middle; the first and last reach the island's edges.
    const int pitch = layout.metrics.icon + layout.metrics.gap;
    const int offset = point.x - (layout.items.front().left - layout.metrics.gap / 2);
    return static_cast<std::size_t>(std::clamp(offset / pitch, 0, static_cast<int>(layout.items.size()) - 1));
}

} // namespace harbor::island
