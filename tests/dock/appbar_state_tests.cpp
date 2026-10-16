#include <gtest/gtest.h>
#include <optional>

#include "core/appbar/appbar_state.h"

namespace harbor::appbar
{
namespace
{

using geometry::Rect;
using namespace std::chrono_literals;

// Explorer's AppBar bookkeeping as far as the dock relies on it: one bottom bar (ours), kept above the
// taskbar while the taskbar isn't auto-hidden. The same calls as the dock's SHAppBarMessage wrapper.
struct FakeExplorer
{
    Rect monitor{0, 0, 1920, 1080};
    int taskbarHeight = 48;
    bool taskbarAutoHide = false;
    bool running = true;

    bool registered = false;
    std::optional<Rect> reserved;
    int registers = 0;
    int removes = 0;
    int queries = 0;
    int sets = 0;

    bool Register()
    {
        ++registers;
        // ABM_NEW fails for a window that is already registered.
        if (!running || registered)
        {
            return false;
        }
        registered = true;
        return true;
    }
    void Remove()
    {
        ++removes;
        registered = false;
        reserved.reset();
    }
    Rect Query(Rect rect)
    {
        ++queries;
        return Allowed(rect);
    }
    Rect Set(Rect rect)
    {
        ++sets;
        reserved = Allowed(rect);
        return *reserved;
    }
    // Explorer restarts: it forgets every AppBar.
    void Restart()
    {
        registered = false;
        reserved.reset();
    }

  private:
    Rect Allowed(Rect rect) const
    {
        const int limit = taskbarAutoHide ? monitor.bottom : monitor.bottom - taskbarHeight;
        if (rect.bottom > limit)
        {
            rect.top -= rect.bottom - limit;
            rect.bottom = limit;
        }
        return rect;
    }
};

class AppBarStateTest : public ::testing::Test
{
  protected:
    // Runs every step the state machine asks for at `now`. Returns the decision it stopped at.
    Decision Settle()
    {
        for (int step = 0; step < 20; ++step)
        {
            const auto decision = RunStep(state, explorer, now);
            if (decision.action == Action::None)
            {
                return decision;
            }
        }
        ADD_FAILURE() << "the AppBar never settled";
        return {};
    }

    void WantBottomStrip(int height = 80)
    {
        state.SetWant(Want{explorer.monitor, height});
    }

    FakeExplorer explorer;
    AppBarState state;
    Milliseconds now{100'000};
};

TEST_F(AppBarStateTest, ReservesTheBottomStripWhenTheTaskbarIsHidden)
{
    explorer.taskbarAutoHide = true;
    WantBottomStrip();

    EXPECT_EQ(Settle(), Decision{});
    EXPECT_EQ(explorer.reserved, (Rect{0, 1000, 1920, 1080}));
    EXPECT_EQ(state.Granted(), explorer.reserved);
    EXPECT_EQ(explorer.registers, 1);
    EXPECT_EQ(explorer.sets, 1);
}

TEST_F(AppBarStateTest, SitsAboveAVisibleTaskbarAtFullHeight)
{
    WantBottomStrip();
    Settle();
    // Explorer moved the queried strip up; the dock keeps its height there.
    EXPECT_EQ(explorer.reserved, (Rect{0, 952, 1920, 1032}));
}

// Explorer sends ABN_POSCHANGED in bursts: they cost one query per interval and no ABM_SETPOS, so they can't
// set off more notifications.
TEST_F(AppBarStateTest, ABurstOfNotificationsCostsOneQueryAndNoSet)
{
    WantBottomStrip();
    Settle();
    const int queries = explorer.queries;

    for (int i = 0; i < 5; ++i)
    {
        state.OnPositionMayHaveChanged();
    }
    // Too soon after the last position: wait.
    EXPECT_EQ(Settle(), (Decision{Action::None, now + 250ms}));
    EXPECT_EQ(explorer.queries, queries);

    now += 250ms;
    EXPECT_EQ(Settle(), Decision{});
    EXPECT_EQ(explorer.queries, queries + 1);
    EXPECT_EQ(explorer.sets, 1);
}

TEST_F(AppBarStateTest, MovesDownWhenTheTaskbarHides)
{
    WantBottomStrip();
    Settle();

    explorer.taskbarAutoHide = true;
    state.OnPositionMayHaveChanged();
    now += 1s;
    Settle();

    EXPECT_EQ(explorer.reserved, (Rect{0, 1000, 1920, 1080}));
    EXPECT_EQ(explorer.sets, 2);
}

TEST_F(AppBarStateTest, ANewHeightIsSetRightAway)
{
    explorer.taskbarAutoHide = true;
    WantBottomStrip();
    Settle();

    now += 1s;
    WantBottomStrip(160);
    Settle();
    EXPECT_EQ(explorer.reserved, (Rect{0, 920, 1920, 1080}));
}

TEST_F(AppBarStateTest, RegistersAgainAfterExplorerRestarts)
{
    WantBottomStrip();
    Settle();

    explorer.Restart();
    state.OnTaskbarCreated();
    now += 1s;
    Settle();

    EXPECT_EQ(explorer.registers, 2);
    EXPECT_TRUE(explorer.registered);
    EXPECT_EQ(explorer.reserved, (Rect{0, 952, 1920, 1032}));
}

// TaskbarCreated also comes when only the DPI changed and the registration is still there: removing first
// keeps ABM_NEW from failing.
TEST_F(AppBarStateTest, TaskbarCreatedWhileStillRegisteredRemovesFirst)
{
    WantBottomStrip();
    Settle();

    state.OnTaskbarCreated();
    now += 1s;
    Settle();

    EXPECT_EQ(explorer.removes, 1);
    EXPECT_EQ(explorer.registers, 2);
    EXPECT_TRUE(explorer.registered);
    EXPECT_TRUE(explorer.reserved);
}

TEST_F(AppBarStateTest, RetriesRegistrationWhenExplorerIsDown)
{
    explorer.running = false;
    WantBottomStrip();

    EXPECT_EQ(Settle(), (Decision{Action::None, now + 2s}));
    EXPECT_EQ(explorer.registers, 1);

    explorer.running = true;
    now += 2s;
    Settle();
    EXPECT_TRUE(explorer.registered);
    EXPECT_TRUE(explorer.reserved);
}

TEST_F(AppBarStateTest, RemovesTheStripWhenNoLongerWanted)
{
    WantBottomStrip();
    Settle();

    state.SetWant(std::nullopt);
    EXPECT_EQ(Settle(), Decision{});
    EXPECT_FALSE(explorer.registered);
    EXPECT_FALSE(explorer.reserved);
    EXPECT_FALSE(state.Granted());
    EXPECT_EQ(explorer.removes, 1);
}

TEST_F(AppBarStateTest, DoesNothingUntilWanted)
{
    EXPECT_EQ(Settle(), Decision{});
    state.OnPositionMayHaveChanged();
    EXPECT_EQ(Settle(), Decision{});
    EXPECT_EQ(explorer.registers + explorer.queries + explorer.removes, 0);
}

// The overlay dock (docs/adr/0014): registered only to hear full-screen notifications, never a strip.
TEST_F(AppBarStateTest, NotifyOnlyRegistersAndReservesNothing)
{
    WantBottomStrip(0);
    EXPECT_EQ(Settle(), Decision{});
    EXPECT_TRUE(explorer.registered);
    EXPECT_EQ(explorer.queries + explorer.sets, 0);
    EXPECT_FALSE(explorer.reserved);
    EXPECT_FALSE(state.Granted());

    // Explorer's notifications and restarts never make it position.
    state.OnPositionMayHaveChanged();
    EXPECT_EQ(Settle(), Decision{});
    explorer.Restart();
    state.OnTaskbarCreated();
    EXPECT_EQ(Settle(), Decision{});
    EXPECT_TRUE(explorer.registered);
    EXPECT_EQ(explorer.queries + explorer.sets, 0);
}

TEST_F(AppBarStateTest, GoingNotifyOnlyLetsGoOfTheStrip)
{
    explorer.taskbarAutoHide = true;
    WantBottomStrip();
    Settle();
    ASSERT_TRUE(explorer.reserved);

    // The strip is given back by removing the AppBar, then registering it again notify-only.
    WantBottomStrip(0);
    EXPECT_EQ(Settle(), Decision{});
    EXPECT_TRUE(explorer.registered);
    EXPECT_FALSE(explorer.reserved);
    EXPECT_FALSE(state.Granted());
    EXPECT_EQ(explorer.removes, 1);
    EXPECT_EQ(explorer.registers, 2);
}

} // namespace
} // namespace harbor::appbar
