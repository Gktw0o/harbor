#include <gtest/gtest.h>

#include "core/island/full_screen.h"

namespace harbor::island
{
namespace
{

using geometry::Rect;

constexpr Rect kMonitor{0, 0, 1920, 1080};
constexpr Rect kOther{1920, 0, 3840, 1080};

// A game: exactly the monitor. A maximized window with the taskbar on auto-hide: past its edges.
const Foreground kGame{1, kMonitor};
const Foreground kMaximized{2, {-8, -8, 1928, 1088}};
const Foreground kNormal{3, {100, 100, 900, 700}};

TEST(FullScreenApp, GoesOnWhileItsWindowStaysInFrontAndCoversTheMonitor)
{
    FullScreenApp app;
    app.Open(kMonitor, kGame);
    EXPECT_TRUE(app.LookAgain(kMonitor, kGame));
    EXPECT_TRUE(app.IsOpen());
}

TEST(FullScreenApp, EndsWhenAnotherWindowComesInFront)
{
    FullScreenApp app;
    app.Open(kMonitor, kGame);
    EXPECT_FALSE(app.LookAgain(kMonitor, kNormal));
    EXPECT_FALSE(app.IsOpen());
    // Over until Explorer says so again: the game coming back doesn't reopen it.
    EXPECT_FALSE(app.LookAgain(kMonitor, kGame));
}

TEST(FullScreenApp, AMaximizedWindowIsNotTheFullScreenApp)
{
    // It covers the monitor too while the taskbar auto-hides, but it isn't the window that went full screen.
    FullScreenApp app;
    app.Open(kMonitor, kGame);
    EXPECT_FALSE(app.LookAgain(kMonitor, kMaximized));
}

TEST(FullScreenApp, EndsWhenItsWindowLeavesFullScreenWithoutLosingTheFocus)
{
    // A browser's Esc: the same window, in front, now smaller.
    FullScreenApp app;
    app.Open(kMonitor, kGame);
    EXPECT_FALSE(app.LookAgain(kMonitor, Foreground{kGame.window, kNormal.bounds}));
}

TEST(FullScreenApp, OnlyItsOwnMonitorCounts)
{
    // In front, but full screen on the other monitor.
    FullScreenApp app;
    app.Open(kMonitor, Foreground{1, kOther});
    EXPECT_FALSE(app.LookAgain(kMonitor, Foreground{1, kOther}));
}

TEST(FullScreenApp, NothingToJudgeChangesNothing)
{
    // Test docks judge only their own windows: the user's foreground changes don't end a test's full screen.
    FullScreenApp app;
    app.Open(kMonitor, std::nullopt);
    EXPECT_TRUE(app.LookAgain(kMonitor, std::nullopt));
    EXPECT_TRUE(app.IsOpen());
}

TEST(FullScreenApp, AnUnknownAppIsTheFirstWindowInFrontThatCoversTheMonitor)
{
    // Explorer's notice came while nothing covering the monitor was in front (a moment between two windows).
    FullScreenApp app;
    app.Open(kMonitor, kNormal);
    EXPECT_TRUE(app.LookAgain(kMonitor, kGame));
    // From then on, it is that window.
    EXPECT_FALSE(app.LookAgain(kMonitor, kMaximized));
}

TEST(FullScreenApp, AnUnknownAppEndsWhenAWindowInFrontDoesntCoverTheMonitor)
{
    FullScreenApp app;
    app.Open(kMonitor, std::nullopt);
    EXPECT_FALSE(app.LookAgain(kMonitor, kNormal));
}

TEST(FullScreenApp, ClosedByExplorer)
{
    FullScreenApp app;
    app.Open(kMonitor, kGame);
    app.Close();
    EXPECT_FALSE(app.IsOpen());
    EXPECT_FALSE(app.LookAgain(kMonitor, kGame));
}

} // namespace
} // namespace harbor::island
