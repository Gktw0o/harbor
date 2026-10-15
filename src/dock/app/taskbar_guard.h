#pragma once

#include <windows.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <string>
#include <string_view>
#include <thread>

#include "adapters/process_job.h"
#include "adapters/win32_taskbar.h"
#include "app/explorer_worker.h"
#include "app/hidden_windows.h"
#include "core/ipc/messages.h"
#include "core/taskbar/guard_policy.h"
#include "ports/logger.h"

namespace harbor::dock
{

// The dock's taskbar safety thread: it alone decides whether the taskbar is hidden (by GuardPolicy's rules)
// and carries that out. It owns a hidden top-level window, which receives the panic hotkey, session-end
// messages, TaskbarCreated (Explorer restarted) and display changes, plus a WinEvent hook that sees Explorer
// show the taskbar again. It never blocks: anything that can wait on Explorer goes to its ExplorerWorker. So
// the panic hotkey keeps working, and the ping timeout keeps being checked, while the dock's UI thread is
// hung.
//
// The pipe thread feeds it directly (Post* below, all thread-safe), not through the UI thread, so a hung UI
// can't hold back a lost link either.
class TaskbarGuard
{
  public:
    struct Hotkey
    {
        UINT modifiers = 0;
        UINT key = 0;
    };
    // Ctrl+Alt+Shift+H (CLAUDE.md rule 1).
    static constexpr Hotkey kPanicHotkey{MOD_CONTROL | MOD_ALT | MOD_SHIFT, 'H'};
    // Test docks use a key no keyboard has, so they never take the real hotkey from a running Harbor.
    static constexpr Hotkey kTestHotkey{MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_F24};
    static constexpr int kPanicHotkeyId = 1;
    // The guard window's class; its title is the control pipe's name, so tests can find a dock's guard.
    static constexpr wchar_t kWindowClass[] = L"Harbor.Dock.Guard";
    // Posted to the state window (SetStateWindow): wParam = hidden (0/1), lParam = ipc::TaskbarBlock.
    static constexpr UINT kStateMessage = WM_APP + 20;
    // Posted to the state window (the island) when hiding is asked for again after the panic hotkey hid it.
    static constexpr UINT kShowIslandMessage = WM_APP + 21;
    // Posted to the state window when the panic hotkey is pressed: close menus and anything else in the way.
    static constexpr UINT kPanicMessage = WM_APP + 22;

    struct Options
    {
        Hotkey hotkey = kPanicHotkey;
        std::wstring windowTitle;
        taskbar::GuardPolicyConfig policy;
        // Passed to Win32Taskbar::Restore.
        std::chrono::milliseconds verifyDelay{1000};
        // Whether this process runs in a kill-on-close job. In-process tests replace it: test runners such as
        // ctest run their tests in one.
        std::function<bool()> inKillOnCloseJob = [] {
            return adapters::CurrentProcessJob() == adapters::ProcessJob::KillOnClose;
        };
    };

    // All three must outlive the guard.
    TaskbarGuard(adapters::Win32Taskbar& taskbar, HiddenWindows& hidden, log::ILogger& log, Options options);
    ~TaskbarGuard();
    TaskbarGuard(const TaskbarGuard&) = delete;
    TaskbarGuard& operator=(const TaskbarGuard&) = delete;

    // Starts the thread and waits for its window. False if the window couldn't be created.
    bool Start();

    // Restores the taskbar (waiting a few seconds at most for Explorer) and ends the thread. Called when the
    // dock exits normally; safe to call more than once.
    void Stop() noexcept;

    // Thread-safe inputs.
    void PostLinkChanged(bool connected) noexcept;
    void PostPing() noexcept;
    void PostRequest(bool hidden) noexcept;
    // Whether the island is on screen (GuardPolicy rule 7).
    void PostPresentable(bool presentable) noexcept;

    // The island. kStateMessage goes there whenever the state changes, and on every new connection; the panic
    // hotkey hides it (a way out of the dock, too) until hiding is asked for again.
    void SetStateWindow(HWND window) noexcept
    {
        m_stateWindow.store(window);
    }

    // The last state reported, for tests.
    ipc::TaskbarState State() const noexcept;

    HWND Window() const noexcept
    {
        return m_window.load();
    }
    bool HotkeyRegistered() const noexcept
    {
        return m_hotkeyRegistered.load();
    }

  private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static void CALLBACK OnWinEvent(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG object, LONG child,
                                    DWORD thread, DWORD time);
    void Run(std::promise<bool>& started) noexcept;
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    void RegisterPanicHotkey(HWND hwnd);
    void Post(UINT message, WPARAM wParam = 0) const noexcept;

    // Applies the policy's decision: hides, restores, or neither; then reports the state.
    void Reevaluate(std::string_view reason, bool forceReport = false);
    void Hide();
    // Shows the hidden windows right away, then has the worker restore auto-hide from the record. With
    // `wait`, returns once that finished or a few seconds passed.
    void RestoreNow(std::string_view reason, bool sessionEnding, bool wait);
    void WatchForReshow(HWND taskbarWindow);
    void OnTaskbarShown(HWND window);
    void Report(ipc::TaskbarBlock blocked, bool force);
    static taskbar::Milliseconds Now() noexcept;

    adapters::Win32Taskbar& m_taskbar;
    HiddenWindows& m_hiddenWindows;
    log::ILogger& m_log;
    const Options m_options;

    // Guard thread only.
    taskbar::GuardPolicy m_policy;
    bool m_hidden = false;
    bool m_sessionEnding = false;
    bool m_stopping = false;
    bool m_reported = false;
    bool m_islandHiddenByPanic = false;
    HWINEVENTHOOK m_hook = nullptr;
    DWORD m_hookProcess = 0;
    UINT m_taskbarCreatedMessage = 0;

    std::thread m_thread;
    std::atomic<HWND> m_window{nullptr};
    std::atomic<HWND> m_stateWindow{nullptr};
    std::atomic<bool> m_hotkeyRegistered{false};
    std::atomic<bool> m_stateHidden{false};
    std::atomic<ipc::TaskbarBlock> m_stateBlocked{ipc::TaskbarBlock::None};
    // Declared last: destroyed first, so its pending restore runs while everything above still exists.
    ExplorerWorker m_worker;
};

} // namespace harbor::dock
