#include "app/taskbar_guard.h"

#include <format>
#include <future>

namespace harbor::dock
{
namespace
{

constexpr UINT kStopMessage = WM_APP + 1;
// Windows gives a session-end handler about 5 s.
constexpr std::chrono::seconds kRestoreWait{4};

} // namespace

TaskbarGuard::TaskbarGuard(adapters::Win32Taskbar& taskbar, HiddenWindows& hidden, log::ILogger& log,
                           Options options)
    : m_taskbar(taskbar), m_hidden(hidden), m_log(log), m_options(std::move(options))
{
}

TaskbarGuard::~TaskbarGuard()
{
    Stop();
}

bool TaskbarGuard::Start()
{
    std::promise<bool> started;
    auto ready = started.get_future();
    m_thread = std::thread([this, &started] { Run(started); });
    return ready.get();
}

void TaskbarGuard::Stop() noexcept
{
    if (!m_thread.joinable())
    {
        return;
    }
    if (const HWND window = m_window.load())
    {
        PostMessageW(window, kStopMessage, 0, 0);
    }
    m_thread.join();
}

void TaskbarGuard::Run(std::promise<bool>& started) noexcept
{
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &TaskbarGuard::WndProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kWindowClass;
    // Fails harmlessly when an earlier guard in this process registered it.
    RegisterClassExW(&windowClass);

    // Top-level (not message-only), because broadcasts such as TaskbarCreated only reach top-level windows.
    // Never shown.
    const HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, m_options.windowTitle.c_str(), WS_POPUP,
                                      0, 0, 0, 0, nullptr, nullptr, instance, this);
    if (hwnd == nullptr)
    {
        m_log.Error(std::format("taskbar guard: can't create its window (error {})", GetLastError()));
        started.set_value(false);
        return;
    }
    m_window.store(hwnd);
    RegisterPanicHotkey(hwnd);
    // `started` belongs to Start(), which returns now.
    started.set_value(true);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        DispatchMessageW(&message);
    }
}

void TaskbarGuard::RegisterPanicHotkey(HWND hwnd)
{
    if (RegisterHotKey(hwnd, kPanicHotkeyId, m_options.hotkey.modifiers | MOD_NOREPEAT, m_options.hotkey.key))
    {
        m_hotkeyRegistered.store(true);
        m_log.Info("taskbar guard: panic hotkey registered");
    }
    else
    {
        m_log.Error(
            std::format("taskbar guard: can't register the panic hotkey (error {}); the dock won't hide "
                        "the taskbar",
                        GetLastError()));
    }
}

LRESULT CALLBACK TaskbarGuard::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<TaskbarGuard*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr)
    {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    if (message == WM_NCDESTROY)
    {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    return self->HandleMessage(hwnd, message, wParam, lParam);
}

LRESULT TaskbarGuard::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_HOTKEY:
        if (wParam == kPanicHotkeyId)
        {
            m_log.Warn("taskbar: panic hotkey pressed");
            RestoreNow("panic hotkey", /*sessionEnding=*/false, /*wait=*/false);
        }
        return 0;
    case WM_QUERYENDSESSION:
        // Restore inside the handler: once the session ends, the process can be killed at any moment.
        RestoreNow("the session is ending", /*sessionEnding=*/true, /*wait=*/true);
        return TRUE;
    case WM_ENDSESSION:
        if (wParam)
        {
            RestoreNow("the session is ending", /*sessionEnding=*/true, /*wait=*/true);
        }
        else
        {
            m_log.Info("taskbar guard: the session end was cancelled");
        }
        return 0;
    case kStopMessage:
        RestoreNow("the dock is exiting", GetSystemMetrics(SM_SHUTTINGDOWN) != 0, /*wait=*/true);
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (m_hotkeyRegistered.exchange(false))
        {
            UnregisterHotKey(hwnd, kPanicHotkeyId);
        }
        m_window.store(nullptr);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

void TaskbarGuard::RestoreNow(std::string_view reason, bool sessionEnding, bool wait)
{
    m_log.Info(std::format("taskbar: restoring because {}", reason));
    // Visible at once, without waiting on Explorer.
    m_hidden.ForEach([](HWND window) { ShowWindowAsync(window, SW_SHOWNA); });
    m_hidden.Clear();
    m_taskbar.ShowHiddenWindows();

    auto restore = [this, sessionEnding] {
        m_taskbar.Restore({.sessionEnding = sessionEnding, .verifyDelay = m_options.verifyDelay});
    };
    if (!wait)
    {
        m_worker.Post(std::move(restore));
    }
    else if (!m_worker.PostAndWait(std::move(restore), kRestoreWait))
    {
        m_log.Error("taskbar: Explorer didn't finish the restore in time");
    }
}

} // namespace harbor::dock
