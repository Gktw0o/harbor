#pragma once

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ports/logger.h"

namespace harbor::adapters
{

// Which windows Harbor treats as the taskbar.
// - Real: Explorer's Shell_TrayWnd and Shell_SecondaryTrayWnd (owned by the shell process) and the real
//   auto-hide setting.
// - Test: windows of the classes "Harbor.Test.<id>.TrayWnd" and "Harbor.Test.<id>.SecondaryTrayWnd" that a
//   test creates, and an auto-hide "setting" kept in <dataDir>\test-autohide-state.txt. Tests never touch
//   the user's taskbar, and never create windows with Explorer's class names (that has broken Explorer for
//   other projects).
class TaskbarTarget
{
  public:
    static TaskbarTarget Real();
    static TaskbarTarget Test(std::wstring_view id, const std::filesystem::path& dataDir);

    bool IsReal() const noexcept
    {
        return m_real;
    }
    const std::wstring& PrimaryClass() const noexcept
    {
        return m_primaryClass;
    }
    const std::wstring& SecondaryClass() const noexcept
    {
        return m_secondaryClass;
    }
    const std::filesystem::path& TestAutoHideFile() const noexcept
    {
        return m_autoHideFile;
    }

  private:
    TaskbarTarget(bool real, std::wstring primaryClass, std::wstring secondaryClass,
                  std::filesystem::path autoHideFile);

    bool m_real;
    std::wstring m_primaryClass;
    std::wstring m_secondaryClass;
    std::filesystem::path m_autoHideFile;
};

// <dataDir>\taskbar-restore.json (see core/taskbar/restore_record.h). Empty if dataDir is.
std::filesystem::path RestoreRecordPath(const std::filesystem::path& dataDir);

struct TaskbarWindow
{
    HWND hwnd = nullptr;
    bool primary = false;
};

struct RestoreOptions
{
    // The session is ending: restore, but keep the record, because Explorer may save its settings before
    // ours land. The next start applies it again.
    bool sessionEnding = false;
    // ABM_SETSTATE has been seen to revert itself, so the state is checked again after this long.
    std::chrono::milliseconds verifyDelay{1000};
};

struct RestoreResult
{
    int windowsShown = 0;
    // The auto-hide state that was put back from the record, if there was one and it held.
    std::optional<std::uint32_t> autoHideRestoredTo;
    // A record is still on disk: unverified, unreadable, or the session is ending.
    bool recordKept = false;
};

// The taskbar as Harbor sees it: find its windows, read and set auto-hide, and restore everything.
// Restoring needs no state from whoever hid the taskbar, so any Harbor process can do it, any number of
// times.
class Win32Taskbar
{
  public:
    Win32Taskbar(TaskbarTarget target, std::filesystem::path recordPath, log::ILogger& log);

    // The taskbar's windows, primary first. Empty if there is no taskbar (e.g. Explorer isn't running).
    std::vector<TaskbarWindow> FindWindows() const;

    // ABM_GETSTATE bits (ABS_AUTOHIDE = 1), or the test file's value (0 if it doesn't exist).
    std::uint32_t GetAutoHideState() const;
    bool SetAutoHideState(std::uint32_t state) const;

    // Shows the primary taskbar and any secondary one on a connected monitor that is hidden. Never blocks
    // (ShowWindowAsync), so it is safe to call while Explorer or the dock is unresponsive.
    int ShowHiddenWindows() const;

    // ShowHiddenWindows, then: if a restore record exists, puts auto-hide back as recorded (off if the record
    // is unreadable) and deletes the record once that is verified. Can block on Explorer for a while.
    RestoreResult Restore(const RestoreOptions& options);

  private:
    bool AutoHideIs(std::uint32_t state) const;

    const TaskbarTarget m_target;
    const std::filesystem::path m_recordPath;
    log::ILogger& m_log;
};

} // namespace harbor::adapters
