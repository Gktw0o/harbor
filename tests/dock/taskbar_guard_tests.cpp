// The guard thread against stand-in taskbar windows. Its hotkey is the test one (Ctrl+Alt+Shift+F24), and
// tests post WM_HOTKEY instead of pressing keys: synthetic input is unreliable on CI machines.

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
using test::FakeTaskbar;

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

    bool WaitForAutoHide(std::uint32_t state) const
    {
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (fake.AutoHide() != state)
        {
            if (std::chrono::steady_clock::now() > deadline)
            {
                return false;
            }
            Sleep(10);
        }
        return true;
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
        {.hotkey = TaskbarGuard::kTestHotkey, .windowTitle = L"guard-test", .verifyDelay = 0ms}};
};

TEST_F(TaskbarGuardTest, RegistersThePanicHotkey)
{
    EXPECT_TRUE(guard.HotkeyRegistered());
}

TEST_F(TaskbarGuardTest, PanicHotkeyRestoresTheTaskbar)
{
    SimulateHidden();
    PostMessageW(guard.Window(), WM_HOTKEY, TaskbarGuard::kPanicHotkeyId, 0);

    EXPECT_TRUE(FakeTaskbar::WaitUntilVisible(fake.Primary(), true));
    EXPECT_TRUE(FakeTaskbar::WaitUntilVisible(fake.Secondaries()[0], true));
    EXPECT_TRUE(WaitForAutoHide(0));
    EXPECT_TRUE(log.Contains("panic hotkey pressed"));
}

TEST_F(TaskbarGuardTest, SessionEndRestoresInsideTheHandlerAndKeepsTheRecord)
{
    SimulateHidden();
    // Sent, not posted: like Windows, which waits for the answer.
    EXPECT_EQ(SendMessageW(guard.Window(), WM_QUERYENDSESSION, 0, ENDSESSION_LOGOFF), TRUE);

    // Done by the time the handler returned.
    EXPECT_EQ(fake.AutoHide(), 0u);
    EXPECT_TRUE(RecordExists());
    EXPECT_TRUE(FakeTaskbar::WaitUntilVisible(fake.Primary(), true));
}

TEST_F(TaskbarGuardTest, StopRestoresTheTaskbar)
{
    SimulateHidden();
    guard.Stop();

    EXPECT_EQ(fake.AutoHide(), 0u);
    EXPECT_FALSE(RecordExists());
    EXPECT_TRUE(FakeTaskbar::WaitUntilVisible(fake.Primary(), true));
    EXPECT_EQ(guard.Window(), nullptr);
}

TEST(TaskbarGuard, ReportsAHotkeyAnotherAppHolds)
{
    test::TempDir dir;
    test::CapturingLogger log;
    FakeTaskbar fake(dir.Path());
    adapters::Win32Taskbar taskbar(fake.Target(), adapters::RestoreRecordPath(dir.Path()), log);
    HiddenWindows hidden;

    const auto hotkey = TaskbarGuard::kTestHotkey;
    ASSERT_TRUE(RegisterHotKey(nullptr, 7, hotkey.modifiers | MOD_NOREPEAT, hotkey.key));
    TaskbarGuard guard(taskbar, hidden, log, {.hotkey = hotkey, .windowTitle = L"guard-test"});
    const bool started = guard.Start();
    UnregisterHotKey(nullptr, 7);

    EXPECT_TRUE(started);
    EXPECT_FALSE(guard.HotkeyRegistered());
    EXPECT_TRUE(log.Contains("can't register the panic hotkey"));
}

} // namespace
} // namespace harbor::dock
