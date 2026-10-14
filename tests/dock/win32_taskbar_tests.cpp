// The restore path against stand-in taskbar windows (TaskbarTarget::Test). Harbor.Core.Tests runs the same
// cases against TaskbarRestorer, the Shell's implementation.

#include <gtest/gtest.h>

#include "adapters/win32_taskbar.h"
#include "capturing_logger.h"
#include "core/taskbar/restore_record.h"
#include "fake_taskbar.h"
#include "temp_dir.h"

namespace harbor::adapters
{
namespace
{

using namespace std::chrono_literals;
using test::FakeTaskbar;

constexpr RestoreOptions kNoDelay{.verifyDelay = 0ms};

class Win32TaskbarTest : public ::testing::Test
{
  protected:
    void WriteRecord(std::string_view text) const
    {
        std::ofstream(RecordPath(), std::ios::binary | std::ios::trunc) << text;
    }

    std::filesystem::path RecordPath() const
    {
        return RestoreRecordPath(dir.Path());
    }

    bool RecordExists() const
    {
        return std::filesystem::exists(RecordPath());
    }

    test::TempDir dir;
    test::CapturingLogger log;
    FakeTaskbar fake{dir.Path(), {.secondaries = 1, .offscreenSecondaries = 1}};
    Win32Taskbar taskbar{fake.Target(), RecordPath(), log};
};

TEST_F(Win32TaskbarTest, FindsTheTaskbarWindowsPrimaryFirst)
{
    const auto windows = taskbar.FindWindows();
    ASSERT_EQ(windows.size(), 3u);
    EXPECT_EQ(windows[0].hwnd, fake.Primary());
    EXPECT_TRUE(windows[0].primary);
    EXPECT_FALSE(windows[1].primary);
    EXPECT_FALSE(windows[2].primary);
}

TEST_F(Win32TaskbarTest, RestoreShowsHiddenTaskbarWindows)
{
    FakeTaskbar::Hide(fake.Primary());
    FakeTaskbar::Hide(fake.Secondaries()[0]);

    EXPECT_EQ(taskbar.Restore(kNoDelay).windowsShown, 2);
    EXPECT_TRUE(FakeTaskbar::WaitUntilVisible(fake.Primary(), true));
    EXPECT_TRUE(FakeTaskbar::WaitUntilVisible(fake.Secondaries()[0], true));
}

TEST_F(Win32TaskbarTest, LeavesASecondaryTaskbarOnAMissingMonitorAlone)
{
    FakeTaskbar::Hide(fake.OffscreenSecondaries()[0]);

    EXPECT_EQ(taskbar.Restore(kNoDelay).windowsShown, 0);
    EXPECT_FALSE(IsWindowVisible(fake.OffscreenSecondaries()[0]));
}

TEST_F(Win32TaskbarTest, LeavesOtherWindowsAlone)
{
    FakeTaskbar other(dir.Path());
    FakeTaskbar::Hide(other.Primary());

    EXPECT_EQ(taskbar.Restore(kNoDelay).windowsShown, 0);
    EXPECT_FALSE(IsWindowVisible(other.Primary()));
}

TEST_F(Win32TaskbarTest, WithoutARecordAutoHideIsLeftAlone)
{
    fake.SetAutoHide(1);

    const auto result = taskbar.Restore(kNoDelay);
    EXPECT_EQ(fake.AutoHide(), 1u);
    EXPECT_FALSE(result.autoHideRestoredTo);
    EXPECT_FALSE(result.recordKept);
}

TEST_F(Win32TaskbarTest, AppliesTheRecordThenDeletesIt)
{
    fake.SetAutoHide(1);
    WriteRecord(taskbar::EncodeRestoreRecord({0, "2026-10-14T15:30:00Z"}));

    const auto result = taskbar.Restore(kNoDelay);
    EXPECT_EQ(fake.AutoHide(), 0u);
    EXPECT_EQ(result.autoHideRestoredTo, 0u);
    EXPECT_FALSE(result.recordKept);
    EXPECT_FALSE(RecordExists());
    EXPECT_TRUE(log.Contains("auto-hide restored to off (recorded 2026-10-14T15:30:00Z)"));
}

TEST_F(Win32TaskbarTest, ADamagedRecordRestoresAutoHideOff)
{
    fake.SetAutoHide(1);
    WriteRecord("{\"v\":1,\"stateBefore\":");

    const auto result = taskbar.Restore(kNoDelay);
    EXPECT_EQ(fake.AutoHide(), 0u);
    EXPECT_EQ(result.autoHideRestoredTo, 0u);
    EXPECT_FALSE(RecordExists());
}

TEST_F(Win32TaskbarTest, KeepsTheRecordWhileTheSessionEnds)
{
    fake.SetAutoHide(1);
    WriteRecord(taskbar::EncodeRestoreRecord({0, "2026-10-14T15:30:00Z"}));

    const auto result = taskbar.Restore({.sessionEnding = true, .verifyDelay = 0ms});
    EXPECT_EQ(fake.AutoHide(), 0u);
    EXPECT_TRUE(result.recordKept);
    EXPECT_TRUE(RecordExists());
}

TEST_F(Win32TaskbarTest, KeepsTheRecordWhenAutoHideDoesNotChangeBack)
{
    fake.SetAutoHide(1);
    WriteRecord(taskbar::EncodeRestoreRecord({0, "2026-10-14T15:30:00Z"}));
    // A read-only file makes the change fail, like Explorer ignoring or reverting ABM_SETSTATE.
    SetFileAttributesW(fake.AutoHideFile().c_str(), FILE_ATTRIBUTE_READONLY);

    const auto result = taskbar.Restore(kNoDelay);
    SetFileAttributesW(fake.AutoHideFile().c_str(), FILE_ATTRIBUTE_NORMAL);
    EXPECT_EQ(fake.AutoHide(), 1u);
    EXPECT_FALSE(result.autoHideRestoredTo);
    EXPECT_TRUE(result.recordKept);
    EXPECT_TRUE(RecordExists());
}

TEST_F(Win32TaskbarTest, RestoreIsIdempotent)
{
    fake.SetAutoHide(1);
    FakeTaskbar::Hide(fake.Primary());
    WriteRecord(taskbar::EncodeRestoreRecord({0, "2026-10-14T15:30:00Z"}));

    taskbar.Restore(kNoDelay);
    ASSERT_TRUE(FakeTaskbar::WaitUntilVisible(fake.Primary(), true));
    const auto second = taskbar.Restore(kNoDelay);
    EXPECT_EQ(second.windowsShown, 0);
    EXPECT_FALSE(second.autoHideRestoredTo);
    EXPECT_EQ(fake.AutoHide(), 0u);
}

TEST_F(Win32TaskbarTest, ShowHiddenWindowsNeverTouchesAutoHide)
{
    fake.SetAutoHide(1);
    FakeTaskbar::Hide(fake.Primary());
    WriteRecord(taskbar::EncodeRestoreRecord({0, "2026-10-14T15:30:00Z"}));

    EXPECT_EQ(taskbar.ShowHiddenWindows(), 1);
    EXPECT_TRUE(FakeTaskbar::WaitUntilVisible(fake.Primary(), true));
    EXPECT_EQ(fake.AutoHide(), 1u);
    EXPECT_TRUE(RecordExists());
}

// Read-only look at the real taskbar: finding windows never changes anything.
TEST(Win32TaskbarReal, FindsOnlyWindowsOwnedByTheShell)
{
    const HWND shell = GetShellWindow();
    if (shell == nullptr)
    {
        GTEST_SKIP() << "no shell window in this session (Explorer isn't running)";
    }
    DWORD shellProcess = 0;
    GetWindowThreadProcessId(shell, &shellProcess);

    test::CapturingLogger log;
    const Win32Taskbar real(TaskbarTarget::Real(), {}, log);
    const auto windows = real.FindWindows();
    for (const auto& window : windows)
    {
        DWORD owner = 0;
        GetWindowThreadProcessId(window.hwnd, &owner);
        EXPECT_EQ(owner, shellProcess);
    }
    if (!windows.empty())
    {
        wchar_t windowClass[64]{};
        GetClassNameW(windows[0].hwnd, windowClass, 64);
        EXPECT_STREQ(windowClass, L"Shell_TrayWnd");
        EXPECT_TRUE(windows[0].primary);
    }
}

} // namespace
} // namespace harbor::adapters
