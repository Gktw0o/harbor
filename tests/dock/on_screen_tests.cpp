#include <gtest/gtest.h>
#include <vector>

#include "core/island/on_screen.h"

namespace harbor::island
{
namespace
{

using geometry::Rect;

// A 1920 x 1080 primary and a 1280 x 1024 monitor to its left.
const std::vector<Rect> kMonitors{{0, 0, 1920, 1080}, {-1280, 56, 0, 1080}};

WindowFacts Shown(Rect window)
{
    return {.committed = true, .visible = true, .cloaked = false, .window = window};
}

TEST(OnScreen, ACommittedVisibleIslandInsideAMonitorIs)
{
    EXPECT_TRUE(IsOnScreen(Shown({847, 1008, 1073, 1072}), kMonitors));
    // Touching the monitor's edges is still inside.
    EXPECT_TRUE(IsOnScreen(Shown({0, 1000, 1920, 1080}), kMonitors));
}

TEST(OnScreen, WorksOnAMonitorLeftOfThePrimary)
{
    EXPECT_TRUE(IsOnScreen(Shown({-672, 952, -608, 1016}), kMonitors));
}

TEST(OnScreen, NotBeforeItsFrameIsCommitted)
{
    auto facts = Shown({847, 1008, 1073, 1072});
    facts.committed = false;
    EXPECT_FALSE(IsOnScreen(facts, kMonitors));
}

TEST(OnScreen, NotWhileHidden)
{
    auto facts = Shown({847, 1008, 1073, 1072});
    facts.visible = false;
    EXPECT_FALSE(IsOnScreen(facts, kMonitors));
}

TEST(OnScreen, NotWhileCloaked)
{
    // On another virtual desktop, the island is visible to Windows but not to the user.
    auto facts = Shown({847, 1008, 1073, 1072});
    facts.cloaked = true;
    EXPECT_FALSE(IsOnScreen(facts, kMonitors));
}

TEST(OnScreen, NotWhenStraddlingTwoMonitors)
{
    EXPECT_FALSE(IsOnScreen(Shown({-100, 1000, 100, 1064}), kMonitors));
}

TEST(OnScreen, NotWhereAMonitorWasUnplugged)
{
    EXPECT_FALSE(IsOnScreen(Shown({2500, 1008, 2726, 1072}), kMonitors));
    EXPECT_FALSE(IsOnScreen(Shown({847, 1008, 1073, 1072}), {}));
}

TEST(OnScreen, NotPartlyOffTheScreen)
{
    EXPECT_FALSE(IsOnScreen(Shown({847, 1040, 1073, 1104}), kMonitors));
}

TEST(OnScreen, AnEmptyWindowIsNot)
{
    EXPECT_FALSE(IsOnScreen(Shown({960, 1040, 960, 1040}), kMonitors));
}

} // namespace
} // namespace harbor::island
