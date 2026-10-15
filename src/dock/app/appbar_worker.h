#pragma once

#include <windows.h>

#include <chrono>
#include <functional>
#include <memory>
#include <thread>

#include "core/appbar/appbar_state.h"
#include "ports/app_bar.h"

namespace harbor::dock
{

// Runs the dock's AppBar (core/appbar/appbar_state.h) on its own thread: SHAppBarMessage is a synchronous
// call into Explorer, which must stall neither the island nor the taskbar guard. The thread owns the port,
// the state and the callback, so one stuck in Explorer can be left behind when the dock exits.
class AppBarWorker
{
  public:
    // onGranted runs on the worker's thread with each new strip Explorer grants.
    AppBarWorker(std::unique_ptr<appbar::IAppBar> appBar,
                 std::function<void(const geometry::Rect&)> onGranted, appbar::AppBarConfig config = {});
    // Stop().
    ~AppBarWorker();
    AppBarWorker(const AppBarWorker&) = delete;
    AppBarWorker& operator=(const AppBarWorker&) = delete;

    // Thread-safe inputs.
    void SetWant(const appbar::Want& want);
    // ABN_POSCHANGED, or a display or work-area change.
    void PositionMayHaveChanged();
    // TaskbarCreated: Explorer restarted, or the primary DPI changed.
    void TaskbarCreated();

    // Removes the AppBar and ends the thread, waiting at most `wait`; a thread still stuck in Explorer after
    // that is abandoned. Safe to call more than once.
    void Stop(std::chrono::milliseconds wait = std::chrono::seconds(2)) noexcept;

  private:
    struct Shared;
    static void Run(const std::shared_ptr<Shared>& shared) noexcept;

    std::shared_ptr<Shared> m_shared;
    std::thread m_thread;
};

} // namespace harbor::dock
