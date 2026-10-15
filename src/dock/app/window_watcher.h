#pragma once

#include <windows.h>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "core/windows/app_windows.h"
#include "ports/logger.h"
#include "ports/window_system.h"

namespace harbor::dock
{

// Follows the desktop's app windows on its own thread (docs/adr/0010), so reading windows and their apps
// never stalls the island, and hands the result to the UI thread as snapshots (newest wins). The thread owns
// the port: one stuck in another app can be left behind at exit. Logs counts only, never names.
class WindowWatcher
{
  public:
    struct Snapshot
    {
        // In the order their processes started.
        std::vector<windows::AppWindows::App> apps;
        bool movingOrSizing = false;
    };

    // classPrefix: see WindowTracker. onChanged runs on the watcher's thread whenever a new snapshot waits in
    // TakeSnapshot; it should only post a message.
    WindowWatcher(std::unique_ptr<windows::IWindowSystem> system, std::string classPrefix, log::ILogger& log,
                  std::function<void()> onChanged);
    // Stop().
    ~WindowWatcher();
    WindowWatcher(const WindowWatcher&) = delete;
    WindowWatcher& operator=(const WindowWatcher&) = delete;

    // Thread-safe. The newest snapshot, or nullopt if none came since the last call.
    std::optional<Snapshot> TakeSnapshot();
    // Thread-safe. Explorer restarted: look at every window again.
    void Rescan();
    // Thread-safe. The dock is about to bring window forward: the flash Windows may raise for it isn't the
    // app asking for attention (WindowTracker::NoteDockActivation).
    void NoteActivation(windows::WindowId window);
    // Ends the thread, waiting at most `wait`; a thread still stuck after that is abandoned. Safe to call
    // more than once.
    void Stop(std::chrono::milliseconds wait = std::chrono::seconds(2)) noexcept;

  private:
    struct Shared;
    static void Run(const std::shared_ptr<Shared>& shared) noexcept;

    std::shared_ptr<Shared> m_shared;
    std::thread m_thread;
};

} // namespace harbor::dock
