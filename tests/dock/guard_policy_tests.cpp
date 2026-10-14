#include <gtest/gtest.h>

#include "core/taskbar/guard_policy.h"

namespace harbor::taskbar
{
namespace
{

using namespace std::chrono_literals;
using ipc::TaskbarBlock;

constexpr GuardPolicyConfig kConfig{
    .stablePings = 3, .pingTimeout = 5000ms, .fightLimit = 3, .fightWindow = 10000ms};
constexpr GuardDecision kHide{true, TaskbarBlock::None};
constexpr GuardDecision kShow{false, TaskbarBlock::None};

// A policy where everything allows hiding: the island is on screen, the hotkey is registered, the link is up
// with enough pings, the last at t = 1000 ms, and the Shell asked.
GuardPolicy Allowed()
{
    GuardPolicy policy(kConfig);
    policy.OnPresentable(true);
    policy.OnHotkeyRegistered(true);
    policy.OnLinkUp();
    for (int i = 0; i < 3; ++i)
    {
        policy.OnPing(1000ms);
    }
    policy.OnRequest(true);
    return policy;
}

TEST(GuardPolicy, HidesWhenEverythingHolds)
{
    EXPECT_EQ(Allowed().Evaluate(1000ms), kHide);
}

// Without the island on screen the user would have neither taskbar nor dock. Not a block the user must clear:
// hiding resumes once it's back.
TEST(GuardPolicy, HidesOnlyWhileTheIslandIsOnScreen)
{
    auto policy = Allowed();
    policy.OnPresentable(false);
    EXPECT_EQ(policy.Evaluate(1000ms), kShow);
    policy.OnPresentable(true);
    EXPECT_EQ(policy.Evaluate(1000ms), kHide);

    GuardPolicy fresh(kConfig);
    fresh.OnHotkeyRegistered(true);
    fresh.OnLinkUp();
    for (int i = 0; i < 3; ++i)
    {
        fresh.OnPing(0ms);
    }
    fresh.OnRequest(true);
    EXPECT_EQ(fresh.Evaluate(0ms), kShow) << "the island counts as not shown until the dock says it is";
}

TEST(GuardPolicy, NothingHidesWithoutARequest)
{
    auto policy = Allowed();
    policy.OnRequest(false);
    EXPECT_EQ(policy.Evaluate(1000ms), kShow);
}

TEST(GuardPolicy, WaitsForAStableLink)
{
    GuardPolicy policy(kConfig);
    policy.OnPresentable(true);
    policy.OnHotkeyRegistered(true);
    policy.OnLinkUp();
    policy.OnRequest(true);
    policy.OnPing(0ms);
    policy.OnPing(100ms);
    EXPECT_EQ(policy.Evaluate(100ms), kShow);
    policy.OnPing(200ms);
    EXPECT_EQ(policy.Evaluate(200ms), kHide);
}

TEST(GuardPolicy, PingsBeforeTheLinkIsUpDontCount)
{
    GuardPolicy policy(kConfig);
    policy.OnPresentable(true);
    policy.OnHotkeyRegistered(true);
    for (int i = 0; i < 3; ++i)
    {
        policy.OnPing(0ms);
    }
    policy.OnLinkUp();
    policy.OnRequest(true);
    EXPECT_EQ(policy.Evaluate(0ms), kShow);
}

TEST(GuardPolicy, AQuietWatchdogStopsTheHide)
{
    auto policy = Allowed();
    EXPECT_EQ(policy.Evaluate(6000ms), kHide);
    EXPECT_EQ(policy.Evaluate(6001ms), kShow);
    policy.OnPing(6001ms);
    EXPECT_EQ(policy.Evaluate(6001ms), kHide);
}

TEST(GuardPolicy, LosingTheLinkForgetsTheRequest)
{
    auto policy = Allowed();
    policy.OnLinkDown();
    policy.OnLinkUp();
    for (int i = 0; i < 3; ++i)
    {
        policy.OnPing(2000ms);
    }
    // The new connection hasn't asked.
    EXPECT_EQ(policy.Evaluate(2000ms), kShow);
    policy.OnRequest(true);
    EXPECT_EQ(policy.Evaluate(2000ms), kHide);
}

TEST(GuardPolicy, ALinkThatKeepsDroppingNeverHides)
{
    auto policy = Allowed();
    for (int round = 0; round < 5; ++round)
    {
        policy.OnLinkDown();
        policy.OnLinkUp();
        policy.OnRequest(true);
        policy.OnPing(1000ms);
        policy.OnPing(1000ms);
        EXPECT_EQ(policy.Evaluate(1000ms), kShow);
    }
}

TEST(GuardPolicy, NoHotkeyBlocksHiding)
{
    auto policy = Allowed();
    policy.OnHotkeyRegistered(false);
    EXPECT_EQ(policy.Evaluate(1000ms), (GuardDecision{false, TaskbarBlock::HotkeyUnavailable}));
}

TEST(GuardPolicy, AKillOnCloseJobBlocksHiding)
{
    auto policy = Allowed();
    policy.OnKillOnCloseJob(true);
    EXPECT_EQ(policy.Evaluate(1000ms), (GuardDecision{false, TaskbarBlock::KillOnCloseJob}));
}

TEST(GuardPolicy, NoTaskbarBlocksHiding)
{
    auto policy = Allowed();
    policy.OnTaskbarFound(false);
    EXPECT_EQ(policy.Evaluate(1000ms), (GuardDecision{false, TaskbarBlock::TaskbarNotFound}));
    policy.OnTaskbarFound(true);
    EXPECT_EQ(policy.Evaluate(1000ms), kHide);
}

TEST(GuardPolicy, PanicBlocksUntilTheShellAsksToShow)
{
    auto policy = Allowed();
    policy.OnPanic();
    EXPECT_EQ(policy.Evaluate(1000ms), (GuardDecision{false, TaskbarBlock::PanicHotkey}));
    // Asking to hide again doesn't clear it...
    policy.OnRequest(true);
    EXPECT_EQ(policy.Evaluate(1000ms), (GuardDecision{false, TaskbarBlock::PanicHotkey}));
    // ...asking to show does, and then hiding works again.
    policy.OnRequest(false);
    policy.OnRequest(true);
    EXPECT_EQ(policy.Evaluate(1000ms), kHide);
}

TEST(GuardPolicy, PanicWithNothingRequestedLeavesNoBlock)
{
    auto policy = Allowed();
    policy.OnRequest(false);
    policy.OnPanic();
    policy.OnRequest(true);
    EXPECT_EQ(policy.Evaluate(1000ms), kHide);
}

TEST(GuardPolicy, ThePanicBlockOutlivesAReconnect)
{
    auto policy = Allowed();
    policy.OnPanic();
    policy.OnLinkDown();
    policy.OnLinkUp();
    for (int i = 0; i < 3; ++i)
    {
        policy.OnPing(2000ms);
    }
    policy.OnRequest(true);
    EXPECT_EQ(policy.Evaluate(2000ms), (GuardDecision{false, TaskbarBlock::PanicHotkey}));
}

TEST(GuardPolicy, ExplorerFightingTripsAfterTheLimit)
{
    auto policy = Allowed();
    EXPECT_FALSE(policy.OnRehide(1000ms));
    EXPECT_FALSE(policy.OnRehide(1100ms));
    EXPECT_FALSE(policy.OnRehide(1200ms));
    EXPECT_EQ(policy.Evaluate(1200ms), kHide);
    EXPECT_TRUE(policy.OnRehide(1300ms));
    EXPECT_EQ(policy.Evaluate(1300ms), (GuardDecision{false, TaskbarBlock::ExplorerFighting}));
    // Tripped once; later re-hides don't report it again.
    EXPECT_FALSE(policy.OnRehide(1400ms));
}

TEST(GuardPolicy, OldRehidesFallOutOfTheWindow)
{
    auto policy = Allowed();
    policy.OnRehide(0ms);
    policy.OnRehide(1ms);
    policy.OnRehide(2ms);
    // The first three are older than 10 s by now.
    EXPECT_FALSE(policy.OnRehide(10003ms));
    EXPECT_FALSE(policy.OnRehide(10004ms));
    EXPECT_EQ(policy.Evaluate(1000ms), kHide);
}

TEST(GuardPolicy, AShowRequestClearsTheFightingBlockAndItsHistory)
{
    auto policy = Allowed();
    for (int i = 0; i < 4; ++i)
    {
        policy.OnRehide(1000ms);
    }
    policy.OnRequest(false);
    policy.OnRequest(true);
    EXPECT_EQ(policy.Evaluate(1000ms), kHide);
    EXPECT_FALSE(policy.OnRehide(1000ms));
}

} // namespace
} // namespace harbor::taskbar
