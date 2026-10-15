#include "app/launcher.h"

#include <objbase.h>

#include <deque>
#include <future>
#include <mutex>
#include <utility>

namespace harbor::dock
{
namespace
{

// Posted to the launcher thread: there are jobs in the queue.
constexpr UINT kWorkMessage = WM_APP + 1;
constexpr DWORD kStopWaitMs = 2000;

} // namespace

struct Launcher::Shared
{
    HWND window = nullptr;
    std::mutex mutex;
    std::deque<std::pair<std::size_t, ipc::DockItem>> jobs;
    DWORD threadId = 0;
};

Launcher::Launcher(HWND window) : m_shared(std::make_shared<Shared>())
{
    m_shared->window = window;
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

Launcher::~Launcher()
{
    Stop();
}

void Launcher::Launch(std::size_t index, ipc::DockItem item)
{
    {
        std::lock_guard lock(m_shared->mutex);
        m_shared->jobs.emplace_back(index, std::move(item));
    }
    PostThreadMessageW(m_shared->threadId, kWorkMessage, 0, 0);
}

void Launcher::Stop() noexcept
{
    if (!m_thread.joinable())
    {
        return;
    }
    PostThreadMessageW(m_shared->threadId, WM_QUIT, 0, 0);
    if (WaitForSingleObject(m_thread.native_handle(), kStopWaitMs) == WAIT_OBJECT_0)
    {
        m_thread.join();
    }
    else
    {
        // Stuck in a call into Explorer. It holds its own reference to the queue and only posts to the window
        // (harmless once that's gone): leave it, the process is ending.
        m_thread.detach();
    }
}

void Launcher::Run(std::shared_ptr<Shared> shared) noexcept
{
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        if (message.hwnd != nullptr || message.message != kWorkMessage)
        {
            DispatchMessageW(&message);
            continue;
        }
        for (;;)
        {
            std::pair<std::size_t, ipc::DockItem> job;
            {
                std::lock_guard lock(shared->mutex);
                if (shared->jobs.empty())
                {
                    break;
                }
                job = std::move(shared->jobs.front());
                shared->jobs.pop_front();
            }
            auto* report = new (std::nothrow) LaunchReport{job.first, adapters::LaunchApp(job.second)};
            if (report != nullptr &&
                !PostMessageW(shared->window, kLaunchedMessage, 0, reinterpret_cast<LPARAM>(report)))
            {
                delete report;
            }
        }
    }
    if (SUCCEEDED(com))
    {
        CoUninitialize();
    }
}

} // namespace harbor::dock
