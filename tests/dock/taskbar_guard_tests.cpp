// The guard thread against stand-in taskbar windows. Its hotkey is the test one (Ctrl+Alt+Shift+F24), and
// tests post WM_HOTKEY instead of pressing keys: synthetic input is unreliable on CI machines.

#include <atomic>
#include <functional>
#include <future>
#include <gtest/gtest.h>
#include <thread>

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
        // No island here: these tests start with it on screen.
        guard.PostPresentable(true);
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

    // The user clicks the tray button: the guard shows the window (auto-hidden; Explorer then slides it in),
    // here at once on screen.
    bool Peek()
    {
        if (guard.Peek() != fake.Primary())
        {
            return false;
        }
        return WaitFor([&] {
            return log.Contains("Explorer shows it for the peek") && IsWindowVisible(fake.Primary());
        });
    }

    // Waits for condition while Harbor Shell keeps pinging (longer than the 3 s ping timeout, say).
    bool WaitPinging(const std::function<bool()>& condition, std::chrono::milliseconds timeout = 5s)
    {
        return WaitFor(
            [&] {
                guard.PostPing();
                return condition();
            },
            timeout);
    }

    // Explorer slides an auto-hidden taskbar off the screen when the user is done with it.
    void SlideOut()
    {
        SetWindowPos(fake.Primary(), nullptr, -30000, -30000, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
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

// Stands in for the dock's island: a visible 1x1 tool window on its own pumping thread that remembers whether
// the guard asked to show it again.
class TestIsland
{
  public:
    TestIsland()
    {
        std::promise<HWND> created;
        auto window = created.get_future();
        m_thread = std::thread([this, &created] {
            WNDCLASSEXW windowClass{sizeof(windowClass)};
            windowClass.lpfnWndProc = [](HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) -> LRESULT {
                if (message == TaskbarGuard::kShowIslandMessage)
                {
                    reinterpret_cast<TestIsland*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))->m_showRequested =
                        true;
                    ShowWindow(hwnd, SW_SHOWNA);
                    return 0;
                }
                return DefWindowProcW(hwnd, message, wParam, lParam);
            };
            windowClass.hInstance = GetModuleHandleW(nullptr);
            windowClass.lpszClassName = L"Harbor.Test.GuardIsland";
            RegisterClassExW(&windowClass);
            const HWND hwnd =
                CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, windowClass.lpszClassName, L"", WS_POPUP,
                                0, 0, 1, 1, nullptr, nullptr, windowClass.hInstance, nullptr);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
            ShowWindow(hwnd, SW_SHOWNA);
            created.set_value(hwnd);
            MSG message{};
            while (GetMessageW(&message, nullptr, 0, 0) > 0)
            {
                DispatchMessageW(&message);
            }
            DestroyWindow(hwnd);
        });
        m_hwnd = window.get();
    }

    ~TestIsland()
    {
        PostThreadMessageW(GetThreadId(m_thread.native_handle()), WM_QUIT, 0, 0);
        m_thread.join();
    }

    HWND Hwnd() const noexcept
    {
        return m_hwnd;
    }
    bool ShowRequested() const noexcept
    {
        return m_showRequested.load();
    }

  private:
    std::thread m_thread;
    HWND m_hwnd = nullptr;
    std::atomic<bool> m_showRequested{false};
};

TEST_F(TaskbarGuardTest, RegistersThePanicHotkey)
{
    EXPECT_TRUE(guard.HotkeyRegistered());
}

// GuardPolicy rule 7 on the guard thread: the taskbar comes back when the island leaves the screen, and goes
// again when the island returns.
TEST_F(TaskbarGuardTest, HidesOnlyWhileTheIslandIsOnScreen)
{
    ConnectAndRequestHide();
    ASSERT_TRUE(WaitUntilHidden());

    guard.PostPresentable(false);
    ASSERT_TRUE(WaitUntilRestored());
    EXPECT_EQ(guard.State().blocked, TaskbarBlock::None);
    EXPECT_TRUE(log.Contains("the island isn't on screen"));

    guard.PostPresentable(true);
    EXPECT_TRUE(WaitUntilHidden());
}

// While hiding is on, the panic hotkey also takes the island away (a way out of the dock); it comes back when
// hiding is turned on again.
TEST_F(TaskbarGuardTest, ThePanicHotkeyHidesTheIslandUntilHidingIsBackOn)
{
    TestIsland island;
    guard.SetStateWindow(island.Hwnd());
    ConnectAndRequestHide();
    ASSERT_TRUE(WaitUntilHidden());

    PostMessageW(guard.Window(), WM_HOTKEY, TaskbarGuard::kPanicHotkeyId, 0);
    ASSERT_TRUE(WaitUntilRestored());
    EXPECT_TRUE(WaitFor([&] { return !IsWindowVisible(island.Hwnd()); }));

    // What Harbor Shell does: it turns the request off when it sees the panic block, then the user turns
    // hiding back on.
    guard.PostRequest(false);
    EXPECT_FALSE(island.ShowRequested());
    guard.PostRequest(true);
    EXPECT_TRUE(WaitFor([&] { return island.ShowRequested() && IsWindowVisible(island.Hwnd()); }));
}

TEST_F(TaskbarGuardTest, ThePanicHotkeyLeavesTheIslandAloneWhenNothingIsHidden)
{
    TestIsland island;
    guard.SetStateWindow(island.Hwnd());
    PostMessageW(guard.Window(), WM_HOTKEY, TaskbarGuard::kPanicHotkeyId, 0);
    EXPECT_TRUE(WaitFor([&] { return log.Contains("panic hotkey pressed"); }));
    Sleep(200);
    EXPECT_TRUE(IsWindowVisible(island.Hwnd()));
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

// The tray button's peek (docs/adr/0012). The restore paths first: each still gives the taskbar back, with
// auto-hide and the record, while the user peeks at it, and no peek ending later hides it again.

TEST_F(TaskbarGuardTest, WhilePeekingThePanicHotkeyRestores)
{
    ConnectAndRequestHide();
    ASSERT_TRUE(WaitUntilHidden());
    ASSERT_TRUE(Peek());

    PostMessageW(guard.Window(), WM_HOTKEY, TaskbarGuard::kPanicHotkeyId, 0);
    ASSERT_TRUE(WaitUntilRestored());
    EXPECT_EQ(guard.State().blocked, TaskbarBlock::PanicHotkey);
    SlideOut();
    Sleep(1500);
    EXPECT_TRUE(IsWindowVisible(fake.Primary()));
    EXPECT_FALSE(log.Contains("the peek is over"));
}

TEST_F(TaskbarGuardTest, WhilePeekingLosingHarborShellRestores)
{
    ConnectAndRequestHide();
    ASSERT_TRUE(WaitUntilHidden());
    ASSERT_TRUE(Peek());

    guard.PostLinkChanged(false);
    ASSERT_TRUE(WaitUntilRestored());
    SlideOut();
    Sleep(1500);
    EXPECT_TRUE(IsWindowVisible(fake.Primary()));
}

TEST_F(TaskbarGuardTest, WhilePeekingASilentWatchdogRestores)
{
    ConnectAndRequestHide();
    ASSERT_TRUE(WaitUntilHidden());
    ASSERT_TRUE(Peek());
    // No more pings: the 3 s timeout runs out while the taskbar is shown for the peek.
    EXPECT_TRUE(WaitUntilRestored());
    EXPECT_TRUE(log.Contains("watchdog went quiet"));

    // The watchdog back, the taskbar goes again: the old peek didn't outlive the restore, so Explorer showing
    // it now is a re-show.
    guard.PostPing();
    ASSERT_TRUE(WaitUntilHidden());
    ShowWindow(fake.Primary(), SW_SHOWNA);
    EXPECT_TRUE(WaitPinging([&] { return log.Contains("Explorer showed it again"); }));
    EXPECT_TRUE(WaitPinging([&] { return !IsWindowVisible(fake.Primary()); }));
}

TEST_F(TaskbarGuardTest, WhilePeekingStopRestores)
{
    ConnectAndRequestHide();
    ASSERT_TRUE(WaitUntilHidden());
    ASSERT_TRUE(Peek());
    guard.Stop();

    EXPECT_EQ(fake.AutoHide(), 0u);
    EXPECT_FALSE(RecordExists());
    EXPECT_TRUE(FakeTaskbar::WaitUntilVisible(fake.Primary(), true));
}

// The crash handlers show the windows the dock hid: a peek never takes one off that list.
TEST_F(TaskbarGuardTest, WhilePeekingTheCrashHandlersStillKnowTheTaskbar)
{
    ConnectAndRequestHide();
    ASSERT_TRUE(WaitUntilHidden());
    ASSERT_TRUE(Peek());
    bool listed = false;
    hidden.ForEach([&](HWND window) { listed = listed || window == fake.Primary(); });
    EXPECT_TRUE(listed);
}

// Explorer shows the taskbar for the user as often as it likes while they peek: no re-hide, no breaker
// (the limit here is 3). Once it slides out, the dock hides it again; hiding is still on.
TEST_F(TaskbarGuardTest, APeekLeavesTheTaskbarToExplorerUntilItSlidesOut)
{
    ConnectAndRequestHide();
    ASSERT_TRUE(WaitUntilHidden());
    ASSERT_TRUE(Peek());
    for (int i = 0; i < 5; ++i)
    {
        ShowWindow(fake.Secondaries()[0], SW_SHOWNA);
        Sleep(50);
    }
    // Shown for as long as the user stays on it.
    EXPECT_FALSE(WaitPinging([&] { return !IsWindowVisible(fake.Primary()); }, 1500ms));
    EXPECT_TRUE(guard.State().hidden);
    EXPECT_EQ(guard.State().blocked, TaskbarBlock::None);
    EXPECT_FALSE(log.Contains("Explorer showed it again"));

    SlideOut();
    EXPECT_TRUE(WaitPinging([&] { return !IsWindowVisible(fake.Primary()); }));
    EXPECT_TRUE(log.Contains("the peek is over"));
    EXPECT_TRUE(guard.State().hidden);
    EXPECT_EQ(fake.AutoHide(), 1u);

    // Back to normal: Explorer showing it now is a re-show again.
    SetWindowPos(fake.Primary(), nullptr, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(fake.Primary(), SW_SHOWNA);
    EXPECT_TRUE(WaitPinging([&] { return log.Contains("Explorer showed it again"); }));
    EXPECT_TRUE(WaitPinging([&] { return !IsWindowVisible(fake.Primary()); }));
}

// Explorer never slid it in (bringing it forward failed, so it stayed auto-hidden off the screen): the peek
// ends by itself (after 3 s), and the window is hidden again.
TEST_F(TaskbarGuardTest, APeekTheTaskbarNeverAnsweredEnds)
{
    ConnectAndRequestHide();
    ASSERT_TRUE(WaitUntilHidden());
    SlideOut();
    ASSERT_EQ(guard.Peek(), fake.Primary());
    ASSERT_TRUE(WaitFor([&] { return IsWindowVisible(fake.Primary()) != FALSE; }));
    EXPECT_TRUE(WaitPinging([&] { return log.Contains("the peek is over"); }));
    EXPECT_TRUE(WaitPinging([&] { return !IsWindowVisible(fake.Primary()); }));
    EXPECT_TRUE(guard.State().hidden);
}

TEST_F(TaskbarGuardTest, NoPeekWhileTheTaskbarIsShown)
{
    EXPECT_EQ(guard.Peek(), nullptr);
    ConnectAndRequestHide();
    ASSERT_TRUE(WaitUntilHidden());
    guard.PostRequest(false);
    ASSERT_TRUE(WaitUntilRestored());
    EXPECT_EQ(guard.Peek(), nullptr);
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
