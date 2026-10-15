#include <gtest/gtest.h>
#include <vector>

#include "core/island/click_policy.h"

namespace harbor::island
{
namespace
{

using Kind = ClickAction::Kind;
using Window = windows::AppWindows::Window;

TEST(ClickPolicy, AnItemWithoutWindowsStarts)
{
    EXPECT_EQ(DecideClick({}, std::nullopt), (ClickAction{.kind = Kind::Launch}));
    EXPECT_EQ(DecideClick({}, 42), (ClickAction{.kind = Kind::Launch}));
}

TEST(ClickPolicy, AnAppNotInFrontBringsItsMostRecentWindowForward)
{
    const std::vector<Window> windows{{.id = 1, .seen = 1, .active = 5}, {.id = 2, .seen = 2, .active = 9}};
    EXPECT_EQ(DecideClick(windows, 99), (ClickAction{.kind = Kind::Activate, .window = 2}));
    EXPECT_EQ(DecideClick(windows, std::nullopt), (ClickAction{.kind = Kind::Activate, .window = 2}));
}

TEST(ClickPolicy, AMinimizedWindowIsRestoredOnTheWay)
{
    const std::vector<Window> windows{{.id = 1, .seen = 1, .active = 5, .minimized = true}};
    EXPECT_EQ(DecideClick(windows, 99), (ClickAction{.kind = Kind::Activate, .window = 1, .restore = true}));
}

TEST(ClickPolicy, WindowsNeverInFrontGoNewestFirst)
{
    const std::vector<Window> windows{{.id = 1, .seen = 1}, {.id = 2, .seen = 2}};
    EXPECT_EQ(DecideClick(windows, std::nullopt).window, 2u);
}

TEST(ClickPolicy, AnAppInFrontWithOneWindowMinimizes)
{
    const std::vector<Window> windows{{.id = 1, .seen = 1, .active = 5}};
    EXPECT_EQ(DecideClick(windows, 1), (ClickAction{.kind = Kind::Minimize, .window = 1}));
}

TEST(ClickPolicy, AnAppInFrontCyclesInTheOrderWindowsWereFirstSeen)
{
    // Given in another order: the order seen counts, not the order given or the recency.
    const std::vector<Window> windows{
        {.id = 30, .seen = 3, .active = 1},
        {.id = 10, .seen = 1, .active = 7},
        {.id = 20, .seen = 2, .active = 4, .minimized = true},
    };
    EXPECT_EQ(DecideClick(windows, 10), (ClickAction{.kind = Kind::Activate, .window = 20, .restore = true}));
    EXPECT_EQ(DecideClick(windows, 20).window, 30u);
    // After the last, the first.
    EXPECT_EQ(DecideClick(windows, 30).window, 10u);
}

} // namespace
} // namespace harbor::island
