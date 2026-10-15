#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "core/geometry/rect.h"

namespace harbor::island
{

// The dock's look in DIPs (1/96 inch), scaled by the monitor's DPI. Icons shrink to fit a narrow monitor,
// down to kMinIconDip; past that, the items that don't fit aren't shown (an overflow menu comes later).
inline constexpr int kIconDip = 48;
inline constexpr int kMinIconDip = 24;
inline constexpr int kPaddingDip = 8;
inline constexpr int kGapDip = 6;
inline constexpr int kMarginDip = 8;
inline constexpr int kCornerRadiusDip = 18;

// DIPs to physical pixels at dpi (96 = 100 %), rounded to the nearest pixel.
int Scale(int dip, unsigned dpi) noexcept;

// Sizes in physical pixels for one monitor, item count and DPI.
struct Metrics
{
    int icon = 0;
    int padding = 0;
    int gap = 0;
    // Between the island and the screen's bottom edge, and above the island inside the reserved strip.
    int margin = 0;
    int cornerRadius = 0;
    // Items that fit; the rest aren't shown.
    std::size_t visibleCount = 0;

    // The strip the dock reserves at the bottom of the monitor (its AppBar): the island plus a margin
    // above and below it.
    int StripHeight() const noexcept
    {
        return 2 * margin + 2 * padding + icon;
    }
    friend bool operator==(const Metrics&, const Metrics&) = default;
};

Metrics MeasureFor(int monitorWidth, unsigned dpi, std::size_t itemCount) noexcept;

struct Layout
{
    Metrics metrics;
    // The island (and the dock window), centred at the bottom of the strip.
    geometry::Rect island;
    // One icon rectangle per visible item, left to right.
    std::vector<geometry::Rect> items;
};

// Places the island and its items in the strip the AppBar got. With no items, the island is one empty slot
// wide, so the dock never disappears.
Layout Arrange(const geometry::Rect& strip, const Metrics& metrics);

// The item under a point. Every point of the island belongs to the nearest item, gaps and padding included,
// so there's nowhere on it a click does nothing.
std::optional<std::size_t> HitTest(const Layout& layout, geometry::Point point) noexcept;

// What an item shows besides its icon.
struct ItemMarks
{
    // The app has windows: a dot under its icon.
    bool running = false;
    // One of its windows asked for attention (docs/adr/0011): a highlight behind the icon until it comes
    // forward.
    bool attention = false;
    friend bool operator==(const ItemMarks&, const ItemMarks&) = default;
};

// The running dot under item index (which must be visible): a circle centred in the padding below its icon,
// half the padding across (at least 2 px).
geometry::Rect DotFor(const Layout& layout, std::size_t index) noexcept;

} // namespace harbor::island
