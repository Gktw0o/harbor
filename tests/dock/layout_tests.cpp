#include <gtest/gtest.h>

#include "core/island/layout.h"

namespace harbor::island
{
namespace
{

using geometry::Point;
using geometry::Rect;

constexpr unsigned k100Percent = 96;
constexpr unsigned k150Percent = 144;
constexpr unsigned k200Percent = 192;

TEST(Layout, ScalesDipsToPixels)
{
    EXPECT_EQ(Scale(48, k100Percent), 48);
    EXPECT_EQ(Scale(48, k150Percent), 72);
    EXPECT_EQ(Scale(18, k200Percent), 36);
    // 125 %: 6 DIP is 7.5 px, rounded to 8.
    EXPECT_EQ(Scale(6, 120), 8);
}

TEST(Layout, FullSizeIconsWhenTheyFit)
{
    const auto metrics = MeasureFor(1920, k100Percent, 4);
    EXPECT_EQ(
        metrics,
        (Metrics{.icon = 48, .padding = 8, .gap = 6, .margin = 8, .cornerRadius = 18, .visibleCount = 4}));
    // Island 64 px tall with 8 px above and below.
    EXPECT_EQ(metrics.StripHeight(), 80);
    EXPECT_EQ(MeasureFor(3840, k200Percent, 4).StripHeight(), 160);
}

TEST(Layout, CentresTheIslandAtTheBottomOfTheStrip)
{
    const Rect strip{0, 1000, 1920, 1080};
    const auto layout = Arrange(strip, MeasureFor(1920, k100Percent, 4));

    // 8 + 4 * 48 + 3 * 6 + 8 = 226 wide, 64 tall, 8 px above the bottom.
    EXPECT_EQ(layout.island, (Rect{847, 1008, 1073, 1072}));
    ASSERT_EQ(layout.items.size(), 4u);
    EXPECT_EQ(layout.items[0], (Rect{855, 1016, 903, 1064}));
    EXPECT_EQ(layout.items[3], (Rect{1017, 1016, 1065, 1064}));
}

TEST(Layout, FollowsAStripMovedAboveTheTaskbar)
{
    const auto layout = Arrange({0, 952, 1920, 1032}, MeasureFor(1920, k100Percent, 1));
    EXPECT_EQ(layout.island.bottom, 1024);
}

TEST(Layout, WorksOnASecondaryOriginMonitor)
{
    const auto layout = Arrange({-1280, 944, 0, 1024}, MeasureFor(1280, k100Percent, 1));
    EXPECT_EQ(layout.island, (Rect{-672, 952, -608, 1016}));
}

TEST(Layout, AnEmptyDockIsOneEmptySlotWide)
{
    const auto metrics = MeasureFor(1920, k100Percent, 0);
    EXPECT_EQ(metrics.visibleCount, 0u);
    const auto layout = Arrange({0, 1000, 1920, 1080}, metrics);
    EXPECT_EQ(layout.island.Width(), 64);
    EXPECT_TRUE(layout.items.empty());
    EXPECT_FALSE(HitTest(layout, {960, 1040}));
}

TEST(Layout, IconsShrinkToFitANarrowMonitor)
{
    // 1024 - 2 * 8 margin - 2 * 8 padding leaves 992 px for 30 icons and 29 gaps: (992 - 29 * 6) / 30 = 27
    // px.
    const auto shrunk = MeasureFor(1024, k100Percent, 30);
    EXPECT_EQ(shrunk.icon, 27);
    EXPECT_EQ(shrunk.visibleCount, 30u);
    EXPECT_LE(Arrange({0, 0, 1024, 100}, shrunk).island.Width(), 1024 - 2 * 8);
    // Smaller icons, smaller island: the corner radius never exceeds half its height.
    EXPECT_EQ(shrunk.cornerRadius, 18);
    EXPECT_EQ(shrunk.StripHeight(), 16 + 16 + 27);
}

TEST(Layout, WhatDoesntFitAtTheSmallestSizeIsLeftOff)
{
    // 40 icons would be (992 - 39 * 6) / 40 = 18 px, below the 24 px floor.
    const auto metrics = MeasureFor(1024, k100Percent, 40);
    EXPECT_EQ(metrics.icon, 24);
    // (992 + 6) / (24 + 6) = 33 icons.
    EXPECT_EQ(metrics.visibleCount, 33u);
    const auto layout = Arrange({0, 0, 1024, 100}, metrics);
    EXPECT_EQ(layout.items.size(), 33u);
    EXPECT_LE(layout.island.Width(), 1024 - 2 * 8);
}

TEST(Layout, AMonitorTooNarrowForOneIconShowsNone)
{
    const auto metrics = MeasureFor(40, k100Percent, 3);
    EXPECT_EQ(metrics.visibleCount, 0u);
    EXPECT_TRUE(Arrange({0, 0, 40, 100}, metrics).items.empty());
}

TEST(Layout, EveryPointOfTheIslandHitsTheNearestItem)
{
    const auto layout = Arrange({0, 1000, 1920, 1080}, MeasureFor(1920, k100Percent, 3));
    const auto& items = layout.items;

    EXPECT_EQ(HitTest(layout, {items[1].left + 10, items[1].top + 10}), 1u);
    // The island's padding belongs to the end items.
    EXPECT_EQ(HitTest(layout, {layout.island.left, layout.island.top}), 0u);
    EXPECT_EQ(HitTest(layout, {layout.island.right - 1, layout.island.bottom - 1}), 2u);
    // Gaps split down the middle (6 px: 3 each).
    EXPECT_EQ(HitTest(layout, {items[0].right + 2, items[0].top}), 0u);
    EXPECT_EQ(HitTest(layout, {items[0].right + 3, items[0].top}), 1u);
    // Outside the island: nothing, even within the strip.
    EXPECT_FALSE(HitTest(layout, {layout.island.left - 1, items[0].top}));
    EXPECT_FALSE(HitTest(layout, {items[0].left, layout.island.top - 1}));
    EXPECT_FALSE(HitTest(layout, {layout.island.right, items[0].top}));
}

TEST(Layout, ADividerSeparatesPinnedItemsFromRunningApps)
{
    const auto metrics = MeasureFor(1920, k100Percent, 4, /*divider=*/true);
    EXPECT_EQ(metrics.divider, 13);
    const auto layout = Arrange({0, 1000, 1920, 1080}, metrics, 2);

    // 8 + 4 * 48 + 3 * 6 + 7 more for the divider + 8 = 233 wide.
    EXPECT_EQ(layout.island, (Rect{843, 1008, 1076, 1072}));
    EXPECT_EQ(layout.items[1], (Rect{905, 1016, 953, 1064}));
    EXPECT_EQ(layout.items[2], (Rect{966, 1016, 1014, 1064}));
    // A 1 px line in the middle of the 13 px between them, 9 px shorter than the icons at each end.
    EXPECT_EQ(layout.divider, (Rect{959, 1025, 960, 1055}));
    EXPECT_EQ(layout.items[3].right + 8, layout.island.right);

    // The divider's room belongs to the items either side, split in the middle.
    EXPECT_EQ(HitTest(layout, {958, 1040}), 1u);
    EXPECT_EQ(HitTest(layout, {960, 1040}), 2u);
}

TEST(Layout, NoDividerWithoutItemsOnBothSides)
{
    const auto metrics = MeasureFor(1920, k100Percent, 3, /*divider=*/true);
    EXPECT_FALSE(Arrange({0, 1000, 1920, 1080}, metrics, 0).divider);
    EXPECT_FALSE(Arrange({0, 1000, 1920, 1080}, metrics, 3).divider);
    const auto plain = Arrange({0, 1000, 1920, 1080}, MeasureFor(1920, k100Percent, 3), 1);
    EXPECT_FALSE(plain.divider);
    EXPECT_EQ(plain.island.Width(), 8 + 3 * 48 + 2 * 6 + 8);
}

TEST(Layout, TheRunningDotSitsCentredInThePaddingBelowTheIcon)
{
    const auto layout = Arrange({0, 1000, 1920, 1080}, MeasureFor(1920, k100Percent, 4));
    // Item 0 is 855..903 wide, its icon ends at 1064 and the island at 1072: a 4 px dot, 2 px from each.
    EXPECT_EQ(DotFor(layout, 0), (Rect{877, 1066, 881, 1070}));
    EXPECT_EQ(DotFor(layout, 3).left - layout.items[3].left, 22);

    const auto large = Arrange({0, 2000, 3840, 2160}, MeasureFor(3840, k200Percent, 1));
    const auto dot = DotFor(large, 0);
    EXPECT_EQ(dot.Width(), 8);
    EXPECT_EQ(dot.Height(), 8);
    EXPECT_EQ(dot.top - large.items[0].bottom, 4);
    EXPECT_EQ(large.island.bottom - dot.bottom, 4);
}

} // namespace
} // namespace harbor::island
