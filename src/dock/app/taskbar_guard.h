#pragma once

#include <windows.h>

#include <atomic>
#include <chrono>
#include <future>
#include <string>
#include <string_view>
#include <thread>

#include "adapters/win32_taskbar.h"
#include "app/explorer_worker.h"
#include "app/hidden_windows.h"
#include "ports/logger.h"

namespace harbor::dock
{

// The dock's taskbar safety thread. It owns a hidden top-level window, which receives the panic hotkey,
// session-end messages and (from M1's hide path on) TaskbarCreated and display changes. It never blocks:
// anything that can wait on Explorer goes to its ExplorerWorker. So the panic hotkey keeps working while the
// dock's UI thread is hung.
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

    struct Options
    {
        Hotkey hotkey = kPanicHotkey;
        std::wstring windowTitle;
        // Passed to Win32Taskbar::Restore.
        std::chrono::milliseconds verifyDelay{1000};
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
    void Run(std::promise<bool>& started) noexcept;
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    void RegisterPanicHotkey(HWND hwnd);
    // Shows the hidden windows right away, then has the worker restore auto-hide from the record. With
    // `wait`, returns once that finished or a few seconds passed.
    void RestoreNow(std::string_view reason, bool sessionEnding, bool wait);

    adapters::Win32Taskbar& m_taskbar;
    HiddenWindows& m_hidden;
    log::ILogger& m_log;
    const Options m_options;

    std::thread m_thread;
    std::atomic<HWND> m_window{nullptr};
    std::atomic<bool> m_hotkeyRegistered{false};
    // Declared last: destroyed first, so its pending restore runs while everything above still exists.
    ExplorerWorker m_worker;
};

} // namespace harbor::dock
