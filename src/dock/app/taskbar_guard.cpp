#include "app/taskbar_guard.h"

#include <format>

#include "adapters/process_job.h"
#include "app/crash_guard.h"
#include "core/ipc/codec.h"

namespace harbor::dock
{
namespace
{

constexpr UINT kStopMessage = WM_APP + 1;
constexpr UINT kLinkMessage = WM_APP + 2;    // wParam: connected
constexpr UINT kPingMessage = WM_APP + 3;    //
constexpr UINT kRequestMessage = WM_APP + 4; // wParam: hidden
constexpr UINT_PTR kCheckTimer = 1;
// How often a silent watchdog is noticed; well under the ping timeout.
constexpr UINT kCheckIntervalMs = 250;
// Windows gives a session-end handler about 5 s.
constexpr std::chrono::seconds kRestoreWait{4};
constexpr int kRestoreAttempts = 3;
constexpr std::chrono::seconds kRestoreRetryDelay{1};

// Out-of-context WinEvent callbacks run on the thread that set the hook: the guard's.
thread_local TaskbarGuard* t_guard = nullptr;

} // namespace

TaskbarGuard::TaskbarGuard(adapters::Win32Taskbar& taskbar, HiddenWindows& hidden, log::ILogger& log,
                           Options options)
    : m_taskbar(taskbar), m_hiddenWindows(hidden), m_log(log), m_options(std::move(options)),
      m_policy(m_options.policy)
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
    Post(kStopMessage);
    m_thread.join();
}

void TaskbarGuard::PostLinkChanged(bool connected) noexcept
{
    Post(kLinkMessage, connected);
}

void TaskbarGuard::PostPing() noexcept
{
    Post(kPingMessage);
}

void TaskbarGuard::PostRequest(bool hidden) noexcept
{
    Post(kRequestMessage, hidden);
}

ipc::TaskbarState TaskbarGuard::State() const noexcept
{
    return {m_stateHidden.load(), m_stateBlocked.load()};
}

void TaskbarGuard::Post(UINT message, WPARAM wParam) const noexcept
{
    if (const HWND window = m_window.load())
    {
        PostMessageW(window, message, wParam, 0);
    }
}

taskbar::Milliseconds TaskbarGuard::Now() noexcept
{
    // Stops while the computer sleeps, so a sleep never looks like a silent watchdog.
    ULONGLONG hundredNanoseconds = 0;
    QueryUnbiasedInterruptTime(&hundredNanoseconds);
    return taskbar::Milliseconds(hundredNanoseconds / 10'000);
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
    t_guard = this;
    m_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
    RegisterPanicHotkey(hwnd);
    m_policy.OnHotkeyRegistered(m_hotkeyRegistered.load());
    m_policy.OnKillOnCloseJob(m_options.inKillOnCloseJob());
    SetTimer(hwnd, kCheckTimer, kCheckIntervalMs, nullptr);
    m_window.store(hwnd);
    // `started` belongs to Start(), which returns now.
    started.set_value(true);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        DispatchMessageW(&message);
    }
    t_guard = nullptr;
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

void CALLBACK TaskbarGuard::OnWinEvent(HWINEVENTHOOK, DWORD, HWND hwnd, LONG object, LONG child, DWORD, DWORD)
{
    if (t_guard != nullptr && object == OBJID_WINDOW && child == CHILDID_SELF)
    {
        t_guard->OnTaskbarShown(hwnd);
    }
}

LRESULT TaskbarGuard::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == m_taskbarCreatedMessage && message != 0)
    {
        // Explorer restarted: its taskbar windows are new (and shown), so start over with them.
        m_log.Warn("taskbar: Explorer restarted");
        m_hiddenWindows.Clear();
        if (m_hook != nullptr)
        {
            UnhookWinEvent(m_hook);
            m_hook = nullptr;
            m_hookProcess = 0;
        }
        m_hidden = false;
        Reevaluate("Explorer restarted");
        return 0;
    }

    switch (message)
    {
    case kLinkMessage:
        if (wParam != 0)
        {
            m_policy.OnLinkUp();
            // A new connection always hears the current state.
            Reevaluate("connected to Harbor Shell", /*forceReport=*/true);
        }
        else
        {
            m_policy.OnLinkDown();
            Reevaluate("Harbor Shell disconnected");
        }
        return 0;
    case kPingMessage:
        m_policy.OnPing(Now());
        Reevaluate("a ping arrived");
        return 0;
    case kRequestMessage:
        m_policy.OnRequest(wParam != 0);
        Reevaluate(wParam != 0 ? "Harbor Shell asked to hide it" : "Harbor Shell asked to show it");
        return 0;
    case WM_TIMER:
        Reevaluate("Harbor Shell's watchdog went quiet");
        return 0;
    case WM_DISPLAYCHANGE:
        // A new monitor brings a new secondary taskbar.
        if (m_hidden)
        {
            Hide();
        }
        return 0;
    case WM_HOTKEY:
        if (wParam == kPanicHotkeyId)
        {
            m_log.Warn("taskbar: panic hotkey pressed");
            m_policy.OnPanic();
            RestoreNow("of the panic hotkey", /*sessionEnding=*/false, /*wait=*/false);
            Reevaluate("panic hotkey", /*forceReport=*/true);
        }
        return 0;
    case WM_QUERYENDSESSION:
        // Restore inside the handler: once the session ends, the process can be killed at any moment.
        m_sessionEnding = true;
        RestoreNow("the session is ending", /*sessionEnding=*/true, /*wait=*/true);
        Report(m_policy.Evaluate(Now()).blocked, /*force=*/false);
        return TRUE;
    case WM_ENDSESSION:
        if (wParam)
        {
            m_sessionEnding = true;
            RestoreNow("the session is ending", /*sessionEnding=*/true, /*wait=*/true);
            Report(m_policy.Evaluate(Now()).blocked, /*force=*/false);
        }
        else
        {
            m_sessionEnding = false;
            Reevaluate("the session end was cancelled");
        }
        return 0;
    case kStopMessage:
        m_stopping = true;
        RestoreNow("the dock is exiting", GetSystemMetrics(SM_SHUTTINGDOWN) != 0, /*wait=*/true);
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, kCheckTimer);
        if (m_hook != nullptr)
        {
            UnhookWinEvent(m_hook);
            m_hook = nullptr;
        }
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

void TaskbarGuard::Reevaluate(std::string_view reason, bool forceReport)
{
    // The guard's own vetoes on top of the policy.
    const bool vetoed = m_stopping || m_sessionEnding || crash_guard::IsDying();
    auto decision = m_policy.Evaluate(Now());
    if (decision.hide && !vetoed && !m_hidden)
    {
        Hide();
        // Hide refreshed what the policy knows about the taskbar and the job.
        decision = m_policy.Evaluate(Now());
    }
    if ((!decision.hide || vetoed) && m_hidden)
    {
        RestoreNow(reason, /*sessionEnding=*/false, /*wait=*/false);
    }
    Report(decision.blocked, forceReport);
}

void TaskbarGuard::Hide()
{
    const auto windows = m_taskbar.FindWindows();
    m_policy.OnTaskbarFound(!windows.empty());
    m_policy.OnKillOnCloseJob(m_options.inKillOnCloseJob());
    if (!m_policy.Evaluate(Now()).hide)
    {
        return;
    }
    WatchForReshow(windows.front().hwnd);
    if (!m_hidden)
    {
        m_log.Info("taskbar: hiding it (Harbor Shell asked, and every safety rule holds)");
    }
    m_hidden = true;
    m_worker.Post([this] {
        if (crash_guard::IsDying())
        {
            return;
        }
        // Auto-hide first, as other tools do: Explorer gives the screen space back, then the windows go.
        m_taskbar.TurnOnAutoHide();
        for (const HWND window : m_taskbar.HideWindows())
        {
            m_hiddenWindows.Add(window);
        }
    });
}

void TaskbarGuard::WatchForReshow(HWND taskbarWindow)
{
    DWORD process = 0;
    GetWindowThreadProcessId(taskbarWindow, &process);
    if (m_hook != nullptr && process == m_hookProcess)
    {
        return;
    }
    if (m_hook != nullptr)
    {
        UnhookWinEvent(m_hook);
    }
    // Only Explorer's (the taskbar owner's) events, and only "shown": cheap enough to leave on.
    m_hook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, &TaskbarGuard::OnWinEvent,
                             process, 0, WINEVENT_OUTOFCONTEXT);
    m_hookProcess = m_hook != nullptr ? process : 0;
    if (m_hook == nullptr)
    {
        m_log.Warn("taskbar: can't watch for Explorer showing the taskbar again");
    }
}

void TaskbarGuard::OnTaskbarShown(HWND window)
{
    if (!m_hidden || crash_guard::IsDying() || !m_taskbar.IsTaskbarWindow(window))
    {
        return;
    }
    ShowWindowAsync(window, SW_HIDE);
    m_hiddenWindows.Add(window);
    if (m_policy.OnRehide(Now()))
    {
        m_log.Warn(
            "taskbar: Explorer keeps showing the taskbar again; restoring it and giving up until Harbor "
            "Shell asks again");
        Reevaluate("Explorer kept showing it", /*forceReport=*/true);
    }
}

void TaskbarGuard::RestoreNow(std::string_view reason, bool sessionEnding, bool wait)
{
    m_log.Info(std::format("taskbar: restoring because {}", reason));
    m_hidden = false;
    // Visible at once, without waiting on Explorer.
    m_hiddenWindows.ForEach([](HWND window) { ShowWindowAsync(window, SW_SHOWNA); });
    m_hiddenWindows.Clear();
    m_taskbar.ShowHiddenWindows();

    auto restore = [this, sessionEnding] {
        // Explorer has been seen to ignore or revert the change: an unverified restore is tried again a few
        // times (after that, on TaskbarCreated and at the next start). Not while the session ends: there the
        // record stays on purpose.
        for (int attempt = 1;; ++attempt)
        {
            const auto result =
                m_taskbar.Restore({.sessionEnding = sessionEnding, .verifyDelay = m_options.verifyDelay});
            const bool unverified = result.recordKept && !result.autoHideRestoredTo;
            if (sessionEnding || !unverified || attempt == kRestoreAttempts)
            {
                return;
            }
            std::this_thread::sleep_for(kRestoreRetryDelay);
        }
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

void TaskbarGuard::Report(ipc::TaskbarBlock blocked, bool force)
{
    const bool changed = !m_reported || m_stateHidden.load() != m_hidden || m_stateBlocked.load() != blocked;
    m_reported = true;
    m_stateHidden.store(m_hidden);
    m_stateBlocked.store(blocked);
    if (changed)
    {
        m_log.Info(std::format("taskbar state: {}, blocked: {}", m_hidden ? "hidden" : "shown",
                               ipc::ToWireName(blocked)));
    }
    if (changed || force)
    {
        if (const HWND window = m_stateWindow.load())
        {
            PostMessageW(window, kStateMessage, m_hidden, static_cast<LPARAM>(blocked));
        }
    }
}

} // namespace harbor::dock
