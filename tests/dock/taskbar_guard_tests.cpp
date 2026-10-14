// The guard thread against stand-in taskbar windows. Its hotkey is the test one (Ctrl+Alt+Shift+F24), and
// tests post WM_HOTKEY instead of pressing keys: synthetic input is unreliable on CI machines.

#include <functional>
#include <gtest/gtest.h>

#include "app/taskbar_guard.h"
#include "capturing_logger.h"
#include "core/taskbar/restore_record.h"
#include "fake_taskbar.h"
#include "temp_dir.h"

namespace harbor::dock
{
namespace
{

using namespace std::chrono_literals;
using ipc::TaskbarBlock;
using test::FakeTaskbar;

bool WaitFor(const std::function<bool()>& condition, std::chrono::milliseconds timeout = 5s)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!condition())
    {
        if (std::chrono::steady_clock::now() > deadline)
        {
            return false;
        }
        Sleep(10);
    }
    return true;
}

class TaskbarGuardTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        ASSERT_TRUE(guard.Start());
    }

    // What an earlier hide would have left: hidden windows the dock knows about, auto-hide on, a record.
    void SimulateHidden()
    {
        for (const HWND window : {fake.Primary(), fake.Secondaries()[0]})
        {
            FakeTaskbar::Hide(window);
            hidden.Add(window);
        }
        fake.SetAutoHide(1);
        std::ofstream(adapters::RestoreRecordPath(dir.Path()), std::ios::binary)
            << taskbar::EncodeRestoreRecord({0, "2026-10-14T15:30:00Z"});
    }

    // Harbor Shell connected, asked to hide, and pinged enough for the link to count as stable.
    void ConnectAndRequestHide()
    {
        guard.PostLinkChanged(true);
        guard.PostRequest(true);
        for (int i = 0; i < 3; ++i)
        {
            guard.PostPing();
        }
    }

    bool WaitUntilHidden()
    {
        return WaitFor([&] {
            return guard.State().hidden && !IsWindowVisible(fake.Primary()) &&
                   !IsWindowVisible(fake.Secondaries()[0]) && fake.AutoHide() == 1;
        });
    }

    bool WaitUntilRestored()
    {
        return WaitFor([&] {
            return !guard.State().hidden && IsWindowVisible(fake.Primary()) &&
                   IsWindowVisible(fake.Secondaries()[0]) && fake.AutoHide() == 0 && !RecordExists();
        });
    }

    bool RecordExists() const
    {
        return std::filesystem::exists(adapters::RestoreRecordPath(dir.Path()));
    }

    test::TempDir dir;
    test::CapturingLogger log;
    FakeTaskbar fake{dir.Path()};
    adapters::Win32Taskbar taskbar{fake.Target(), adapters::RestoreRecordPath(dir.Path()), log};
    HiddenWindows hidden;
    TaskbarGuard guard{
        taskbar,
        hidden,
        log,
        {
            .hotkey = TaskbarGuard::kTestHotkey,
            .windowTitle = L"guard-test",
            .policy = {.stablePings = 3, .pingTimeout = 3000ms, .fightLimit = 3},
            .verifyDelay = 0ms,
            // ctest runs tests in a kill-on-close job; the real check has its own end-to-end test.
            .inKillOnCloseJob = [] { return false; },
        }};
};

TEST_F(TaskbarGuardTest, RegistersThePanicHotkey)
{
    EXPECT_TRUE(guard.HotkeyRegistered());
}

TEST_F(TaskbarGuardTest, HidesOnceTheWatchdogHasPingedEnough)
{
    guard.PostLinkChanged(true);
    guard.PostRequest(true);
    guard.PostPing();
    guard.PostPing();
    Sleep(200);
    EXPECT_FALSE(guard.State().hidden);
    EXPECT_TRUE(IsWindowVisible(fake.Primary()));

    guard.PostPing();
    ASSERT_TRUE(WaitUntilHidden());
    // Auto-hide was off, so the record holds that.
    const auto record =
        taskbar::DecodeRestoreRecord(test::ReadFileBytes(adapters::RestoreRecordPath(dir.Path())));
    ASSERT_TRUE(record);
    EXPECT_EQ(record->stateBefore, 0u);
}

TEST_F(TaskbarGuardTest, WritesNoRecordWhenAutoHideWasAlreadyOn)
{
    fake.SetAutoHide(1);
    ConnectAndRequestHide();
    ASSERT_TRUE(WaitUntilHidden());
    EXPECT_FALSE(RecordExists());
}

TEST_F(TaskbarGuardTest, RestoresWhenTheShellAsks)
{
    ConnectAndRequestHide();
    ASSERT_TRUE(WaitUntilHidden());
    guard.PostRequest(false);
    EXPECT_TRUE(WaitUntilRestored());
}

// The hide runs on the worker and only queues the change (ShowWindowAsync). A restore right behind it must
// still win, even if the windows still look visible when it runs.
TEST_F(TaskbarGuardTest, ARestoreRightAfterAHideWins)
{
    for (int round = 0; round < 20; ++round)
    {
        ConnectAndRequestHide();
        ASSERT_TRUE(WaitFor([&] { return guard.State().hidden; })) << "round " << round;
        guard.PostRequest(false);
        ASSERT_TRUE(WaitFor([&] { return !guard.State().hidden; })) << "round " << round;
        // Long enough for every queued hide and show to land.
        Sleep(100);
        ASSERT_TRUE(IsWindowVisible(fake.Primary())) << "round " << round;
        ASSERT_TRUE(IsWindowVisible(fake.Secondaries()[0])) << "round " << round;
    }
}

// The first restore doesn't hold (here: the stand-in setting is read-only, like Explorer ignoring the
// change); a retry a second later does.
TEST_F(TaskbarGuardTest, RetriesARestoreThatDidNotHold)
{
    ConnectAndRequestHide();
    ASSERT_TRUE(WaitUntilHidden());
    ASSERT_TRUE(WaitFor([&] { return RecordExists(); }));
    SetFileAttributesW(fake.AutoHideFile().c_str(), FILE_ATTRIBUTE_READONLY);
    guard.PostRequest(false);
    ASSERT_TRUE(WaitFor([&] { return log.Contains("keeping the restore record to try again"); }));
    SetFileAttributesW(fake.AutoHideFile().c_str(), FILE_ATTRIBUTE_NORMAL);

    EXPECT_TRUE(WaitUntilRestored());
}

TEST_F(TaskbarGuardTest, RestoresWhenTheShellDisconnects)
{
    ConnectAndRequestHide();
    ASSERT_TRUE(WaitUntilHidden());
    guard.PostLinkChanged(false);
    EXPECT_TRUE(WaitUntilRestored());

    // The request went with the connection: a new one that only pings doesn't hide.
    guard.PostLinkChanged(true);
    for (int i = 0; i < 3; ++i)
    {
        guard.PostPing();
    }
    Sleep(300);
    EXPECT_FALSE(guard.State().hidden);
}

TEST_F(TaskbarGuardTest, RestoresWhenPingsStop)
{
    ConnectAndRequestHide();
    ASSERT_TRUE(WaitUntilHidden());
    // No more pings: the 3 s timeout runs out.
    EXPECT_TRUE(WaitUntilRestored());
    EXPECT_TRUE(log.Contains("watchdog went quiet"));
}

TEST_F(TaskbarGuardTest, PanicHotkeyRestoresAndBlocksUntilTheShellAsksToShow)
{
    ConnectAndRequestHide();
    ASSERT_TRUE(WaitUntilHidden());
    PostMessageW(guard.Window(), WM_HOTKEY, TaskbarGuard::kPanicHotkeyId, 0);
    ASSERT_TRUE(WaitUntilRestored());
    EXPECT_EQ(guard.State().blocked, TaskbarBlock::PanicHotkey);

    guard.PostRequest(true);
    guard.PostPing();
    Sleep(300);
    EXPECT_FALSE(guard.State().hidden);

    guard.PostRequest(false);
    guard.PostRequest(true);
    guard.PostPing();
    EXPECT_TRUE(WaitUntilHidden());
}

TEST_F(TaskbarGuardTest, PanicHotkeyRestoresWhatAnEarlierRunLeft)
{
    SimulateHidden();
    PostMessageW(guard.Window(), WM_HOTKEY, TaskbarGuard::kPanicHotkeyId, 0);
    EXPECT_TRUE(WaitUntilRestored());
    EXPECT_TRUE(log.Contains("panic hotkey pressed"));
}

TEST_F(TaskbarGuardTest, HidesTheTaskbarAgainWhenExplorerShowsIt)
{
    ConnectAndRequestHide();
    ASSERT_TRUE(WaitUntilHidden());
    ShowWindow(fake.Primary(), SW_SHOWNA);
    EXPECT_TRUE(WaitFor([&] { return !IsWindowVisible(fake.Primary()); }));
    EXPECT_TRUE(guard.State().hidden);
}

TEST_F(TaskbarGuardTest, GivesUpWhenExplorerKeepsShowingIt)
{
    ConnectAndRequestHide();
    ASSERT_TRUE(WaitUntilHidden());
    // The limit is 3 re-hides; the 4th trips the breaker.
    for (int i = 0; i < 3; ++i)
    {
        ShowWindow(fake.Primary(), SW_SHOWNA);
        ASSERT_TRUE(WaitFor([&] { return !IsWindowVisible(fake.Primary()); })) << "re-hide " << i;
    }
    ShowWindow(fake.Primary(), SW_SHOWNA);
    EXPECT_TRUE(WaitUntilRestored());
    EXPECT_EQ(guard.State().blocked, TaskbarBlock::ExplorerFighting);
}

TEST_F(TaskbarGuardTest, SessionEndRestoresInsideTheHandlerAndKeepsTheRecord)
{
    ConnectAndRequestHide();
    ASSERT_TRUE(WaitUntilHidden());
    // Sent, not posted: like Windows, which waits for the answer.
    EXPECT_EQ(SendMessageW(guard.Window(), WM_QUERYENDSESSION, 0, ENDSESSION_LOGOFF), TRUE);

    // Done by the time the handler returned.
    EXPECT_EQ(fake.AutoHide(), 0u);
    EXPECT_TRUE(RecordExists());
    EXPECT_TRUE(FakeTaskbar::WaitUntilVisible(fake.Primary(), true));

    // While the session ends, nothing hides it again.
    guard.PostPing();
    Sleep(300);
    EXPECT_FALSE(guard.State().hidden);
}

TEST_F(TaskbarGuardTest, ACancelledLogoffHidesAgain)
{
    ConnectAndRequestHide();
    ASSERT_TRUE(WaitUntilHidden());
    SendMessageW(guard.Window(), WM_QUERYENDSESSION, 0, ENDSESSION_LOGOFF);
    ASSERT_FALSE(guard.State().hidden);

    SendMessageW(guard.Window(), WM_ENDSESSION, FALSE, ENDSESSION_LOGOFF);
    guard.PostPing();
    EXPECT_TRUE(WaitUntilHidden());
}

TEST_F(TaskbarGuardTest, StopRestoresTheTaskbar)
{
    ConnectAndRequestHide();
    ASSERT_TRUE(WaitUntilHidden());
    guard.Stop();

    EXPECT_EQ(fake.AutoHide(), 0u);
    EXPECT_FALSE(RecordExists());
    EXPECT_TRUE(FakeTaskbar::WaitUntilVisible(fake.Primary(), true));
    EXPECT_EQ(guard.Window(), nullptr);
}

TEST(TaskbarGuard, AHotkeyAnotherAppHoldsBlocksHiding)
{
    test::TempDir dir;
    test::CapturingLogger log;
    FakeTaskbar fake(dir.Path());
    adapters::Win32Taskbar taskbar(fake.Target(), adapters::RestoreRecordPath(dir.Path()), log);
    HiddenWindows hidden;

    const auto hotkey = TaskbarGuard::kTestHotkey;
    ASSERT_TRUE(RegisterHotKey(nullptr, 7, hotkey.modifiers | MOD_NOREPEAT, hotkey.key));
    TaskbarGuard guard(
        taskbar, hidden, log,
        {.hotkey = hotkey, .windowTitle = L"guard-test", .inKillOnCloseJob = [] { return false; }});
    const bool started = guard.Start();
    UnregisterHotKey(nullptr, 7);
    ASSERT_TRUE(started);
    EXPECT_FALSE(guard.HotkeyRegistered());
    EXPECT_TRUE(log.Contains("can't register the panic hotkey"));

    guard.PostLinkChanged(true);
    guard.PostRequest(true);
    for (int i = 0; i < 3; ++i)
    {
        guard.PostPing();
    }
    EXPECT_TRUE(WaitFor([&] { return guard.State().blocked == TaskbarBlock::HotkeyUnavailable; }));
    EXPECT_TRUE(IsWindowVisible(fake.Primary()));
}

} // namespace
} // namespace harbor::dock
