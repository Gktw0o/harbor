#include "app/window_watcher.h"

// WIL's COM helpers need objbase.h first.
#include <objbase.h>

#include <algorithm>
#include <cstdint>
#include <format>
#include <future>
#include <limits>
#include <utility>
#include <wil/resource.h>

#include "app/latest_value.h"
#include "app/window_tracker.h"

namespace harbor::dock
{
namespace
{

constexpr wchar_t kWindowClass[] = L"Harbor.Dock.WindowWatcher";
// Posted to the watcher's (message-only) window. Window messages, not thread messages: a modal loop inside
// a COM call still delivers them.
constexpr UINT kEventsMessage = WM_APP + 1;
constexpr UINT kRescanMessage = WM_APP + 2;
constexpr UINT kStopMessage = WM_APP + 3;
constexpr UINT_PTR kRecheckTimer = 1;

std::chrono::milliseconds Now() noexcept
{
    return std::chrono::milliseconds(GetTickCount64());
}

} // namespace

struct WindowWatcher::Shared
{
    Shared(std::unique_ptr<windows::IWindowSystem> port, std::string prefix, log::ILogger& logger,
           std::function<void()> changed)
        : system(std::move(port)), classPrefix(std::move(prefix)), log(logger), onChanged(std::move(changed))
    {
    }

    // Watcher thread only.
    std::unique_ptr<windows::IWindowSystem> system;
    std::string classPrefix;
    log::ILogger& log;
    std::function<void()> onChanged;
    std::unique_ptr<WindowTracker> tracker;
    HWND window = nullptr;
    // Work() is running: a nested call (from a pumping COM call) only leaves its flag for the loop.
    bool busy = false;
    bool wakePosted = false;
    bool eventsWaiting = false;
    bool rescanWaiting = false;
    bool timerDue = false;
    std::uint64_t publishedRevision = std::numeric_limits<std::uint64_t>::max();
    bool publishedMoving = false;
    std::size_t loggedApps = std::numeric_limits<std::size_t>::max();
    std::size_t loggedWindows = 0;

    // Any thread.
    LatestValue<Snapshot> snapshots;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    void Work();
    void Publish();
};

LRESULT CALLBACK WindowWatcher::Shared::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* shared = reinterpret_cast<Shared*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (shared == nullptr)
    {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    switch (message)
    {
    case kEventsMessage:
        shared->wakePosted = false;
        shared->eventsWaiting = true;
        shared->Work();
        return 0;
    case kRescanMessage:
        shared->rescanWaiting = true;
        shared->Work();
        return 0;
    case WM_TIMER:
        shared->timerDue = true;
        shared->Work();
        return 0;
    case kStopMessage:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

void WindowWatcher::Shared::Work()
{
    if (busy || !tracker)
    {
        return;
    }
    busy = true;
    while (eventsWaiting || rescanWaiting || timerDue)
    {
        const auto now = Now();
        if (std::exchange(rescanWaiting, false))
        {
            tracker->Rescan(now);
        }
        if (std::exchange(eventsWaiting, false))
        {
            bool overflowed = false;
            const auto events = system->TakeEvents(overflowed);
            if (overflowed)
            {
                log.Warn("windows: events were lost; looking at every window again");
                tracker->Rescan(now);
            }
            else
            {
                tracker->OnEvents(events, now);
            }
        }
        if (std::exchange(timerDue, false))
        {
            tracker->OnTimer(now);
        }
    }
    KillTimer(window, kRecheckTimer);
    if (const auto next = tracker->NextTimer())
    {
        const auto delay = std::max<std::int64_t>((*next - Now()).count(), 1);
        SetTimer(window, kRecheckTimer, static_cast<UINT>(std::min<std::int64_t>(delay, 60'000)), nullptr);
    }
    Publish();
    busy = false;
}

void WindowWatcher::Shared::Publish()
{
    const auto& apps = tracker->Apps();
    const bool moving = tracker->MovingOrSizing();
    if (apps.Revision() == publishedRevision && moving == publishedMoving)
    {
        return;
    }
    publishedRevision = apps.Revision();
    publishedMoving = moving;

    std::size_t windowCount = 0;
    for (const auto& app : apps.Apps())
    {
        windowCount += app.windows.size();
    }
    if (apps.Apps().size() != loggedApps || windowCount != loggedWindows)
    {
        loggedApps = apps.Apps().size();
        loggedWindows = windowCount;
        // Counts only: which apps run is the user's business.
        log.Info(std::format("windows: {} apps, {} windows", loggedApps, loggedWindows));
    }
    snapshots.Put({apps.Apps(), moving});
    onChanged();
}

WindowWatcher::WindowWatcher(std::unique_ptr<windows::IWindowSystem> system, std::string classPrefix,
                             log::ILogger& log, std::function<void()> onChanged)
    : m_shared(std::make_shared<Shared>(std::move(system), std::move(classPrefix), log, std::move(onChanged)))
{
    std::promise<HWND> started;
    auto window = started.get_future();
    m_thread = std::thread([shared = m_shared, &started] {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = &Shared::WndProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = kWindowClass;
        RegisterClassExW(&windowClass);
        shared->window = CreateWindowExW(0, kWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                         windowClass.hInstance, nullptr);
        started.set_value(shared->window);
        if (shared->window == nullptr)
        {
            shared->log.Error(std::format("windows: no watcher window ({})", GetLastError()));
            return;
        }
        Run(shared);
    });
    window.wait();
}

WindowWatcher::~WindowWatcher()
{
    Stop();
}

std::optional<WindowWatcher::Snapshot> WindowWatcher::TakeSnapshot()
{
    return m_shared->snapshots.Take();
}

void WindowWatcher::Rescan()
{
    PostMessageW(m_shared->window, kRescanMessage, 0, 0);
}

void WindowWatcher::Stop(std::chrono::milliseconds wait) noexcept
{
    if (!m_thread.joinable())
    {
        return;
    }
    PostMessageW(m_shared->window, kStopMessage, 0, 0);
    if (WaitForSingleObject(m_thread.native_handle(), static_cast<DWORD>(wait.count())) == WAIT_OBJECT_0)
    {
        m_thread.join();
    }
    else
    {
        // Stuck in a call into another app; it owns everything it touches.
        m_thread.detach();
    }
}

void WindowWatcher::Run(const std::shared_ptr<Shared>& shared) noexcept
try
{
    // Shell property stores are COM; this thread pumps messages, so it can be a single-threaded apartment.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const wil::unique_couninitialize_call uninitialize(SUCCEEDED(com));
    SetWindowLongPtrW(shared->window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(shared.get()));

    shared->tracker = std::make_unique<WindowTracker>(*shared->system, shared->classPrefix);
    Shared* raw = shared.get();
    if (!shared->system->Start([raw] {
            // Called from the event hooks, maybe many times in a row: one message is enough.
            if (!raw->wakePosted)
            {
                raw->wakePosted = PostMessageW(raw->window, kEventsMessage, 0, 0) != FALSE;
            }
        }))
    {
        shared->log.Warn("windows: no window events; running apps won't show");
    }
    else
    {
        shared->rescanWaiting = true;
        shared->Work();
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        DispatchMessageW(&message);
    }
    shared->system->Stop();
    SetWindowLongPtrW(shared->window, GWLP_USERDATA, 0);
    DestroyWindow(shared->window);
}
catch (...)
{
    shared->log.Error("windows: the watcher stopped after an unexpected error");
}

} // namespace harbor::dock
