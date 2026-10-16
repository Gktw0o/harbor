#include <gtest/gtest.h>

#include "core/island/tuck.h"

namespace harbor::island
{
namespace
{

using namespace std::chrono_literals;

TEST(Tuck, StandsAsideAWhileAfterAFullScreenAppCame)
{
    Tuck tuck;
    tuck.OnFullScreen(true, 1000ms);
    EXPECT_FALSE(tuck.Tucked());
    EXPECT_EQ(tuck.WakeAt(), 3500ms);
    tuck.OnTimer(3499ms);
    EXPECT_FALSE(tuck.Tucked());
    tuck.OnTimer(3500ms);
    EXPECT_TRUE(tuck.Tucked());
    EXPECT_EQ(tuck.WakeAt(), std::nullopt);
}

TEST(Tuck, ComesBackOnTheMouseAndTucksAgainAfterItLeaves)
{
    Tuck tuck;
    tuck.OnFullScreen(true, 0ms);
    tuck.OnTimer(2500ms);
    ASSERT_TRUE(tuck.Tucked());

    tuck.OnPointer(true, 10'000ms);
    EXPECT_FALSE(tuck.Tucked());
    EXPECT_EQ(tuck.WakeAt(), std::nullopt) << "no tuck while the mouse is on it";
    tuck.OnPointer(false, 12'000ms);
    EXPECT_FALSE(tuck.Tucked());
    EXPECT_EQ(tuck.WakeAt(), 13'000ms);
    tuck.OnTimer(13'000ms);
    EXPECT_TRUE(tuck.Tucked());
}

TEST(Tuck, TheEndOfFullScreenAlwaysBringsItBack)
{
    Tuck tuck;
    tuck.OnFullScreen(true, 0ms);
    tuck.OnTimer(2500ms);
    tuck.OnFullScreen(false, 3000ms);
    EXPECT_FALSE(tuck.Tucked());
    EXPECT_FALSE(tuck.FullScreen());
    EXPECT_EQ(tuck.WakeAt(), std::nullopt);
    // A late timer changes nothing.
    tuck.OnTimer(9000ms);
    EXPECT_FALSE(tuck.Tucked());
}

TEST(Tuck, FullScreenEndingBeforeTheDelayCancelsIt)
{
    Tuck tuck;
    tuck.OnFullScreen(true, 0ms);
    tuck.OnFullScreen(false, 1000ms);
    tuck.OnTimer(2500ms);
    EXPECT_FALSE(tuck.Tucked());
}

TEST(Tuck, NotWhileTheMouseIsOnIt)
{
    Tuck tuck;
    tuck.OnPointer(true, 0ms);
    tuck.OnFullScreen(true, 100ms);
    EXPECT_EQ(tuck.WakeAt(), std::nullopt);
    tuck.OnTimer(10'000ms);
    EXPECT_FALSE(tuck.Tucked());
    tuck.OnPointer(false, 10'000ms);
    EXPECT_EQ(tuck.WakeAt(), 11'000ms);
}

TEST(Tuck, RepeatedNoticesDontRestartTheDelay)
{
    Tuck tuck;
    tuck.OnFullScreen(true, 0ms);
    tuck.OnFullScreen(true, 2000ms);
    EXPECT_EQ(tuck.WakeAt(), 2500ms);
}

TEST(Tuck, ANewMonitorStartsOver)
{
    Tuck tuck;
    tuck.OnFullScreen(true, 0ms);
    tuck.OnTimer(2500ms);
    tuck.OnMonitorChanged();
    EXPECT_FALSE(tuck.Tucked());
    EXPECT_FALSE(tuck.FullScreen());
    EXPECT_EQ(tuck.WakeAt(), std::nullopt);
}

TEST(Tuck, ConfigurableDelays)
{
    Tuck tuck({.delay = 100ms, .afterLeave = 50ms});
    tuck.OnFullScreen(true, 0ms);
    EXPECT_EQ(tuck.WakeAt(), 100ms);
    tuck.OnTimer(100ms);
    tuck.OnPointer(true, 200ms);
    tuck.OnPointer(false, 300ms);
    EXPECT_EQ(tuck.WakeAt(), 350ms);
}

} // namespace
} // namespace harbor::island
