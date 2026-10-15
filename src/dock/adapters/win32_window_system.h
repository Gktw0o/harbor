#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <wil/resource.h>

#include "ports/window_system.h"

namespace harbor::adapters
{

// The desktop's windows through Win32 (docs/adr/0010): out-of-context WinEvent hooks that skip the dock's own
// process and only queue, window styles and DWM's cloak state, the window's or its process's AppUserModelID
// (SHGetPropertyStoreForWindow, the CoreWindow a packaged app's frame hosts, the package), and the process's
// image path and start time. Nothing here sends a message to another process's window. Use on one thread,
// which pumps messages and has COM initialized.
class Win32WindowSystem final : public windows::IWindowSystem
{
  public:
    Win32WindowSystem() = default;
    ~Win32WindowSystem() override;
    Win32WindowSystem(const Win32WindowSystem&) = delete;
    Win32WindowSystem& operator=(const Win32WindowSystem&) = delete;

    bool Start(std::function<void()> wake) override;
    void Stop() noexcept override;
    std::vector<windows::WindowEvent> TakeEvents(bool& overflowed) override;
    std::vector<windows::WindowId> TopLevelWindows() override;
    std::optional<windows::WindowFacts> FactsOf(windows::WindowId window) override;
    std::optional<windows::WindowApp> AppOf(windows::WindowId window) override;
    bool IsMinimized(windows::WindowId window) override;
    bool IsHung(windows::WindowId window) override;
    windows::WindowId RootOwner(windows::WindowId window) override;

    // Events queued beyond this are dropped (and TakeEvents reports it).
    static constexpr std::size_t kMaxQueued = 4096;

  private:
    struct ProcessFacts
    {
        std::uint64_t started = 0;
        std::string exe;
        std::string packageAumid;
    };

    static void CALLBACK OnEvent(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG object, LONG child,
                                 DWORD thread, DWORD time);
    static LRESULT CALLBACK ShellHookProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    void Queue(windows::WindowEvent event);
    // Facts about the process, cached by its id and start time; nullopt if it can't be opened.
    std::optional<ProcessFacts> FactsOfProcess(DWORD processId);

    std::vector<wil::unique_hwineventhook> m_hooks;
    HWND m_shellHookWindow = nullptr;
    UINT m_shellHookMessage = 0;
    std::function<void()> m_wake;
    std::vector<windows::WindowEvent> m_events;
    bool m_overflowed = false;
    std::map<DWORD, ProcessFacts> m_processes;
};

} // namespace harbor::adapters
