#include "app/explorer_worker.h"

namespace harbor::dock
{

ExplorerWorker::ExplorerWorker() : m_thread([state = m_state] { Run(state); })
{
}

ExplorerWorker::~ExplorerWorker()
{
    std::unique_lock lock(m_state->mutex);
    m_state->stopping = true;
    m_state->changed.notify_all();
    if (m_state->changed.wait_for(lock, kStopTimeout, [&] { return m_state->stopped; }))
    {
        lock.unlock();
        m_thread.join();
    }
    else
    {
        // Explorer isn't answering. The thread holds its own reference to the state, so it is safe to leave.
        lock.unlock();
        m_thread.detach();
    }
}

void ExplorerWorker::Post(std::function<void()> job)
{
    Enqueue(std::move(job));
}

bool ExplorerWorker::PostAndWait(std::function<void()> job, std::chrono::milliseconds timeout)
{
    const auto ticket = Enqueue(std::move(job));
    std::unique_lock lock(m_state->mutex);
    return m_state->changed.wait_for(lock, timeout, [&] { return m_state->done >= ticket; });
}

std::uint64_t ExplorerWorker::Enqueue(std::function<void()> job)
{
    std::lock_guard lock(m_state->mutex);
    m_state->pending = std::move(job);
    const auto ticket = ++m_state->posted;
    m_state->changed.notify_all();
    return ticket;
}

void ExplorerWorker::Run(const std::shared_ptr<State>& state) noexcept
{
    for (;;)
    {
        std::function<void()> job;
        std::uint64_t ticket = 0;
        {
            std::unique_lock lock(state->mutex);
            state->changed.wait(lock, [&] { return state->pending || state->stopping; });
            if (!state->pending)
            {
                state->stopped = true;
                state->changed.notify_all();
                return;
            }
            job = std::move(state->pending);
            state->pending = nullptr;
            // Every job posted so far is either this one or was replaced by it.
            ticket = state->posted;
        }

        try
        {
            job();
        }
        catch (...)
        {
            // Jobs log their own failures; one that throws must not stop the ones after it.
        }

        std::lock_guard lock(state->mutex);
        state->done = ticket;
        state->changed.notify_all();
    }
}

} // namespace harbor::dock
