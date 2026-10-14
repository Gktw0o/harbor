#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace harbor::dock
{

// Runs work that can block on Explorer (SHAppBarMessage is a synchronous call into Explorer) on its own
// thread, so the UI and guard threads never wait on Explorer. Only the newest pending job is kept: a
// restore posted while a hide is still waiting replaces it.
class ExplorerWorker
{
  public:
    ExplorerWorker();
    // Runs a job still pending, then stops. If a job is stuck in Explorer for longer than kStopTimeout, the
    // thread is left to finish (or die with the process) instead of hanging the caller.
    ~ExplorerWorker();
    ExplorerWorker(const ExplorerWorker&) = delete;
    ExplorerWorker& operator=(const ExplorerWorker&) = delete;

    static constexpr std::chrono::seconds kStopTimeout{5};

    void Post(std::function<void()> job);

    // Posts the job and waits for it (or a newer job that replaced it) to finish. False on timeout; the job
    // still runs later.
    bool PostAndWait(std::function<void()> job, std::chrono::milliseconds timeout);

  private:
    // Shared with the thread, so the thread can outlive this object if it has to be left behind.
    struct State
    {
        std::mutex mutex;
        std::condition_variable changed;
        std::function<void()> pending;
        std::uint64_t posted = 0;
        std::uint64_t done = 0;
        bool stopping = false;
        bool stopped = false;
    };

    static void Run(const std::shared_ptr<State>& state) noexcept;
    std::uint64_t Enqueue(std::function<void()> job);

    std::shared_ptr<State> m_state = std::make_shared<State>();
    std::thread m_thread;
};

} // namespace harbor::dock
