#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "core/geometry/rect.h"

namespace harbor::island
{

struct MonitorInfo
{
    geometry::Rect bounds;
    geometry::Rect work;
    unsigned dpi = 96;
    bool primary = false;
    friend bool operator==(const MonitorInfo&, const MonitorInfo&) = default;
};

// The monitor the dock is on (docs/adr/0011), given the bounds of the one it was on (nullopt at start): the
// monitor that now holds the centre of those bounds (it may have changed resolution), else the primary (the
// old one went away). nullopt only without monitors.
std::optional<std::size_t> DockMonitor(std::span<const MonitorInfo> monitors,
                                       std::optional<geometry::Rect> previous);

// Where monitor index's edge trigger goes: 1 px rows at the bottom of its strip, raise pixels higher where
// Explorer's auto-hidden taskbar keeps the last pixels, and only where the cursor stops at the bottom (not
// above a part of another monitor it would move on to). Empty if there is no such place.
std::vector<geometry::Rect> EdgeTriggers(std::span<const MonitorInfo> monitors, std::size_t index,
                                         const geometry::Rect& strip, int raise);

// The cursor rests on a trigger: on its row or below it on that monitor, within its columns.
bool RestsOnTrigger(const geometry::Rect& trigger, const MonitorInfo& monitor,
                    geometry::Point cursor) noexcept;

// A trigger window (its row down to the edge) is where the cursor stops on monitor: inside it and reaching
// its bottom edge. Part of rule 7 while the island is tucked (docs/adr/0014).
bool TriggerAtEdge(const geometry::Rect& window, const MonitorInfo& monitor) noexcept;

} // namespace harbor::island
