#pragma once

#include <windows.h>

#include <cstddef>
#include <memory>
#include <thread>

#include "adapters/app_launcher.h"
#include "core/ipc/messages.h"

namespace harbor::dock
{

// One launch's outcome, posted back to the window (Launcher::kLaunchedMessage, lParam), which deletes it.
struct LaunchReport
{
    std::size_t index = 0;
    adapters::LaunchResult result;
};

// Starts apps on its own thread: a single-threaded apartment with a message loop, as Explorer's COM objects
// expect, so a slow start never holds up the island. It touches nothing of the dock but its own queue and the
// window it posts to, so one stuck in a call into Explorer can be left behind when the dock exits.
class Launcher
{
  public:
    static constexpr UINT kLaunchedMessage = WM_APP + 7;

    explicit Launcher(HWND window);
    // Stop().
    ~Launcher();
    Launcher(const Launcher&) = delete;
    Launcher& operator=(const Launcher&) = delete;

    // Queues item i for starting.
    void Launch(std::size_t index, ipc::DockItem item);

    // Ends the thread, waiting 2 s at most; a thread still busy after that is abandoned.
    void Stop() noexcept;

  private:
    struct Shared;
    static void Run(std::shared_ptr<Shared> shared) noexcept;

    std::shared_ptr<Shared> m_shared;
    std::thread m_thread;
};

} // namespace harbor::dock
