#include <gtest/gtest.h>
#include <vector>

#include "core/island/monitors.h"

namespace harbor::island
{
namespace
{

using geometry::Rect;

// A 2880 x 1800 primary at 200 % and a 1920 x 1080 monitor to its right (the development PC).
const std::vector<MonitorInfo> kSideBySide{
    {.bounds = {0, 0, 2880, 1800}, .work = {0, 0, 2880, 1800}, .dpi = 192, .primary = true},
    {.bounds = {2880, 0, 4800, 1080}, .work = {2880, 0, 4800, 1080}, .dpi = 96},
};

TEST(Monitors, TheDockStartsOnThePrimary)
{
    std::vector<MonitorInfo> monitors{kSideBySide[1], kSideBySide[0]};
    EXPECT_EQ(DockMonitor(monitors, std::nullopt), 1u);
    EXPECT_FALSE(DockMonitor({}, std::nullopt));
}

TEST(Monitors, TheDockStaysOnItsMonitorThroughAResolutionChange)
{
    std::vector<MonitorInfo> monitors = kSideBySide;
    monitors[1].bounds = {2880, 0, 5440, 1440};
    EXPECT_EQ(DockMonitor(monitors, Rect{2880, 0, 4800, 1080}), 1u);
}

TEST(Monitors, TheDockGoesBackToThePrimaryWhenItsMonitorGoes)
{
    const std::vector<MonitorInfo> primaryOnly{kSideBySide[0]};
    EXPECT_EQ(DockMonitor(primaryOnly, Rect{2880, 0, 4800, 1080}), 0u);
}

TEST(Monitors, TheTriggerIsTheStripsBottomRow)
{
    const Rect strip{2880, 1000, 4800, 1080};
    EXPECT_EQ(EdgeTriggers(kSideBySide, 1, strip, 0), (std::vector<Rect>{{2880, 1079, 4800, 1080}}));
}

TEST(Monitors, TheTriggerRisesAboveExplorersAutoHiddenTaskbar)
{
    const Rect strip{0, 1640, 2880, 1800};
    EXPECT_EQ(EdgeTriggers(kSideBySide, 0, strip, 2), (std::vector<Rect>{{0, 1797, 2880, 1798}}));
}

TEST(Monitors, NoTriggerWhereTheCursorWouldMoveOnToAMonitorBelow)
{
    // A laptop under a monitor: the cursor never stops at the top monitor's bottom edge.
    const std::vector<MonitorInfo> stacked{
        {.bounds = {0, -1080, 1920, 0}},
        {.bounds = {0, 0, 1920, 1200}, .primary = true},
    };
    EXPECT_TRUE(EdgeTriggers(stacked, 0, {0, -80, 1920, 0}, 0).empty());
    EXPECT_EQ(EdgeTriggers(stacked, 1, {0, 1120, 1920, 1200}, 0).size(), 1u);
}

TEST(Monitors, ATriggerSplitsAroundAMonitorPartlyBelow)
{
    const std::vector<MonitorInfo> offset{
        {.bounds = {0, -1080, 1920, 0}},
        {.bounds = {500, 0, 1500, 800}, .primary = true},
    };
    EXPECT_EQ(EdgeTriggers(offset, 0, {0, -80, 1920, 0}, 0),
              (std::vector<Rect>{{0, -1, 500, 0}, {1500, -1, 1920, 0}}));
}

TEST(Monitors, Resting)
{
    const auto& monitor = kSideBySide[0];
    const Rect trigger{0, 1797, 2880, 1798};
    EXPECT_TRUE(RestsOnTrigger(trigger, monitor, {100, 1797}));
    // Below the row, on Explorer's last pixels: still resting at the edge.
    EXPECT_TRUE(RestsOnTrigger(trigger, monitor, {100, 1799}));
    EXPECT_FALSE(RestsOnTrigger(trigger, monitor, {100, 1796}));
    EXPECT_FALSE(RestsOnTrigger(trigger, monitor, {2880, 1799}));
    EXPECT_FALSE(RestsOnTrigger(trigger, monitor, {100, 1800}));
}

TEST(Monitors, ATriggerAtTheEdge)
{
    const auto& monitor = kSideBySide[0];
    EXPECT_TRUE(TriggerAtEdge({0, 1797, 2880, 1800}, monitor));
    // Moved off the edge, out of the monitor, or nothing at all: the cursor can't reach it there.
    EXPECT_FALSE(TriggerAtEdge({0, 1700, 2880, 1790}, monitor));
    EXPECT_FALSE(TriggerAtEdge({0, 1797, 2880, 1801}, monitor));
    EXPECT_FALSE(TriggerAtEdge({-10, 1797, 100, 1800}, monitor));
    EXPECT_FALSE(TriggerAtEdge({0, 1800, 2880, 1800}, monitor));
}

} // namespace
} // namespace harbor::island
