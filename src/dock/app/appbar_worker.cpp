#include "app/appbar_worker.h"

#include <algorithm>
#include <future>
#include <mutex>
#include <optional>
#include <utility>

namespace harbor::dock
{
namespace
{

// Posted to the worker's thread: there is input to apply.
constexpr UINT kWakeMessage = WM_APP + 1;

appbar::Milliseconds Now() noexcept
{
    return appbar::Milliseconds(GetTickCount64());
}

} // namespace

struct AppBarWorker::Shared
{
    Shared(std::unique_ptr<appbar::IAppBar> port, std::function<void(const geometry::Rect&)> granted,
           appbar::AppBarConfig config)
        : appBar(std::move(port)), onGranted(std::move(granted)), state(config)
    {
    }

    // Worker thread only.
    std::unique_ptr<appbar::IAppBar> appBar;
    std::function<void(const geometry::Rect&)> onGranted;
    appbar::AppBarState state;
    std::optional<geometry::Rect> reported;

    // Inputs not applied yet.
    std::mutex mutex;
    std::optional<appbar::Want> want;
    bool positionMayHaveChanged = false;
    bool taskbarCreated = false;
    bool stopping = false;

    DWORD threadId = 0;
};

AppBarWorker::AppBarWorker(std::unique_ptr<appbar::IAppBar> appBar,
                           std::function<void(const geometry::Rect&)> onGranted, appbar::AppBarConfig config)
    : m_shared(std::make_shared<Shared>(std::move(appBar), std::move(onGranted), config))
{
    std::promise<DWORD> started;
    auto threadId = started.get_future();
    m_thread = std::thread([shared = m_shared, &started] {
        // A message queue exists once PeekMessage has run: PostThreadMessage works from then on.
        MSG message{};
        PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
        started.set_value(GetCurrentThreadId());
        Run(shared);
    });
    m_shared->threadId = threadId.get();
}

AppBarWorker::~AppBarWorker()
{
    Stop();
}

void AppBarWorker::SetWant(const appbar::Want& want)
{
    {
        std::lock_guard lock(m_shared->mutex);
        m_shared->want = want;
    }
    PostThreadMessageW(m_shared->threadId, kWakeMessage, 0, 0);
}

void AppBarWorker::PositionMayHaveChanged()
{
    {
        std::lock_guard lock(m_shared->mutex);
        m_shared->positionMayHaveChanged = true;
    }
    PostThreadMessageW(m_shared->threadId, kWakeMessage, 0, 0);
}

void AppBarWorker::TaskbarCreated()
{
    {
        std::lock_guard lock(m_shared->mutex);
        m_shared->taskbarCreated = true;
    }
    PostThreadMessageW(m_shared->threadId, kWakeMessage, 0, 0);
}

void AppBarWorker::Stop(std::chrono::milliseconds wait) noexcept
{
    if (!m_thread.joinable())
    {
        return;
    }
    {
        std::lock_guard lock(m_shared->mutex);
        m_shared->stopping = true;
    }
    PostThreadMessageW(m_shared->threadId, kWakeMessage, 0, 0);
    if (WaitForSingleObject(m_thread.native_handle(), static_cast<DWORD>(wait.count())) == WAIT_OBJECT_0)
    {
        m_thread.join();
    }
    else
    {
        // Stuck in a call into Explorer; it owns everything it touches. Explorer frees the strip of a window
        // that's gone anyway.
        m_thread.detach();
    }
}

void AppBarWorker::Run(const std::shared_ptr<Shared>& shared) noexcept
{
    auto& state = shared->state;
    UINT_PTR timer = 0;
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        if (message.hwnd != nullptr)
        {
            DispatchMessageW(&message);
            continue;
        }
        if (message.message != kWakeMessage && message.message != WM_TIMER)
        {
            continue;
        }

        bool stopping = false;
        {
            std::lock_guard lock(shared->mutex);
            if (shared->want)
            {
                state.SetWant(std::exchange(shared->want, std::nullopt));
            }
            if (std::exchange(shared->positionMayHaveChanged, false))
            {
                state.OnPositionMayHaveChanged();
            }
            if (std::exchange(shared->taskbarCreated, false))
            {
                state.OnTaskbarCreated();
            }
            stopping = shared->stopping;
        }
        if (stopping)
        {
            state.SetWant(std::nullopt);
        }

        appbar::Decision decision;
        // Each step changes the state; a handful settle it. The bound only guards against a bug looping.
        for (int step = 0; step < 16; ++step)
        {
            decision = appbar::RunStep(state, *shared->appBar, Now());
            if (decision.action == appbar::Action::None)
            {
                break;
            }
        }

        if (const auto granted = state.Granted(); granted && granted != shared->reported)
        {
            shared->reported = granted;
            shared->onGranted(*granted);
        }
        if (stopping)
        {
            break;
        }

        if (timer != 0)
        {
            KillTimer(nullptr, timer);
            timer = 0;
        }
        if (decision.wakeAt)
        {
            const auto delay = std::max(*decision.wakeAt - Now(), appbar::Milliseconds(1));
            timer = SetTimer(nullptr, 0, static_cast<UINT>(delay.count()), nullptr);
        }
    }
    if (timer != 0)
    {
        KillTimer(nullptr, timer);
    }
}

} // namespace harbor::dock
