#include <gtest/gtest.h>

#include "core/island/slot_map.h"

namespace harbor::island
{
namespace
{

using Kind = SlotMap::Kind;
using Entry = SlotMap::Entry;

std::vector<Entry> Slots(std::size_t from, std::size_t to)
{
    std::vector<Entry> entries;
    for (auto slot = from; slot < to; ++slot)
    {
        entries.push_back({Kind::Slot, slot});
    }
    return entries;
}

TEST(SlotMap, StartFirstThenTheSlotsThenTheTray)
{
    const auto map = ArrangeSlots(true, 3, true, 5);
    std::vector<Entry> expected{{Kind::Start}};
    for (const auto& entry : Slots(0, 3))
    {
        expected.push_back(entry);
    }
    expected.push_back({Kind::Tray});
    EXPECT_EQ(map.entries, expected);
    EXPECT_EQ(map.IndexOf(Kind::Start), 0u);
    EXPECT_EQ(map.IndexOf(Kind::Tray), 4u);
    EXPECT_EQ(map.IndexOf(Kind::Overflow), std::nullopt);
    EXPECT_EQ(map.IndexOfSlot(0), 1u);
    EXPECT_EQ(map.IndexOfSlot(2), 3u);
    EXPECT_EQ(map.IndexOfSlot(3), std::nullopt);
    EXPECT_EQ(map.SlotsShown(), 3u);
}

TEST(SlotMap, WithoutStartOrTrayTheSlotsAreTheIndices)
{
    const auto map = ArrangeSlots(false, 4, false, 4);
    EXPECT_EQ(map.entries, Slots(0, 4));
}

// The M3 rule, with the Start button in front: the last place left for slots becomes "…".
TEST(SlotMap, WhatDoesntFitGoesBehindTheOverflowButtonBeforeTheTray)
{
    const auto map = ArrangeSlots(true, 10, true, 6);
    std::vector<Entry> expected{{Kind::Start}};
    for (const auto& entry : Slots(0, 3))
    {
        expected.push_back(entry);
    }
    expected.push_back({Kind::Overflow, 3});
    expected.push_back({Kind::Tray});
    EXPECT_EQ(map.entries, expected);
    EXPECT_EQ(map.SlotsShown(), 3u);
    EXPECT_EQ(map.IndexOfSlot(3), std::nullopt) << "behind the … button";
    EXPECT_EQ(map.IndexOf(Kind::Overflow), 4u);
}

TEST(SlotMap, OnePlaceForTenSlotsIsTheOverflowButton)
{
    const auto map = ArrangeSlots(true, 10, false, 2);
    EXPECT_EQ(map.entries, (std::vector<Entry>{{Kind::Start}, {Kind::Overflow, 0}}));
}

TEST(SlotMap, TinyIslands)
{
    EXPECT_TRUE(ArrangeSlots(true, 3, true, 0).entries.empty());
    EXPECT_EQ(ArrangeSlots(true, 3, true, 1).entries, (std::vector<Entry>{{Kind::Start}}));
    EXPECT_EQ(ArrangeSlots(true, 3, true, 2).entries, (std::vector<Entry>{{Kind::Start}, {Kind::Tray}}));
    EXPECT_EQ(ArrangeSlots(false, 3, true, 1).entries, (std::vector<Entry>{{Kind::Tray}}));
}

TEST(SlotMap, NoSlots)
{
    EXPECT_EQ(ArrangeSlots(true, 0, false, 1).entries, (std::vector<Entry>{{Kind::Start}}));
    EXPECT_TRUE(ArrangeSlots(false, 0, false, 3).entries.empty());
}

} // namespace
} // namespace harbor::island
