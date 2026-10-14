// Checks that the machine running the tests can run Harbor's safety tests at all: an interactive session
// where global hotkeys and WinEvent hooks work. A CI runner without one would otherwise make other tests
// fail in confusing ways (or pass for the wrong reason).

#include <windows.h>

#include <chrono>
#include <gtest/gtest.h>

namespace
{

HWND g_shown = nullptr;
bool g_sawShow = false;

void CALLBACK OnWinEvent(HWINEVENTHOOK, DWORD, HWND window, LONG object, LONG, DWORD, DWORD)
{
    if (window == g_shown && object == OBJID_WINDOW)
    {
        g_sawShow = true;
    }
}

TEST(Environment, RunsInAnInteractiveSession)
{
    DWORD session = 0;
    ASSERT_TRUE(ProcessIdToSessionId(GetCurrentProcessId(), &session));
    EXPECT_NE(session, 0u)
        << "session 0 has no desktop: hotkeys, taskbar windows and WinEvent hooks don't work";
}

TEST(Environment, CanRegisterAGlobalHotkey)
{
    // F23, so it never collides with the test docks' F24.
    ASSERT_TRUE(RegisterHotKey(nullptr, 1, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, VK_F23))
        << "error " << GetLastError();
    UnregisterHotKey(nullptr, 1);
}

TEST(Environment, WinEventHooksReportWindowsBeingShown)
{
    const HWINEVENTHOOK hook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, &OnWinEvent,
                                               GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
    ASSERT_NE(hook, nullptr);
    g_sawShow = false;
    g_shown = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"", WS_POPUP, 0, 0, 1, 1,
                              nullptr, nullptr, nullptr, nullptr);
    ASSERT_NE(g_shown, nullptr);
    ShowWindow(g_shown, SW_SHOWNA);

    // Out-of-context events arrive through this thread's message queue.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!g_sawShow && std::chrono::steady_clock::now() < deadline)
    {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            DispatchMessageW(&message);
        }
        Sleep(10);
    }
    DestroyWindow(g_shown);
    UnhookWinEvent(hook);
    EXPECT_TRUE(g_sawShow);
}

} // namespace
