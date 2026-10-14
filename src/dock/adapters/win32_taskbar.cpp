#include "adapters/win32_taskbar.h"

#include <charconv>
#include <format>
#include <fstream>
#include <shellapi.h>
#include <sstream>
#include <thread>

#include "core/taskbar/restore_record.h"

namespace harbor::adapters
{
namespace
{

// The file's bytes, or nullopt if it can't be opened.
std::optional<std::string> ReadWholeFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return std::nullopt;
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

const char* AutoHideName(std::uint32_t state) noexcept
{
    return (state & ABS_AUTOHIDE) != 0 ? "on" : "off";
}

} // namespace

TaskbarTarget::TaskbarTarget(bool real, std::wstring primaryClass, std::wstring secondaryClass,
                             std::filesystem::path autoHideFile)
    : m_real(real), m_primaryClass(std::move(primaryClass)), m_secondaryClass(std::move(secondaryClass)),
      m_autoHideFile(std::move(autoHideFile))
{
}

TaskbarTarget TaskbarTarget::Real()
{
    return TaskbarTarget(true, L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd", {});
}

TaskbarTarget TaskbarTarget::Test(std::wstring_view id, const std::filesystem::path& dataDir)
{
    const std::wstring prefix = L"Harbor.Test." + std::wstring(id);
    return TaskbarTarget(false, prefix + L".TrayWnd", prefix + L".SecondaryTrayWnd",
                         dataDir / L"test-autohide-state.txt");
}

std::filesystem::path RestoreRecordPath(const std::filesystem::path& dataDir)
{
    return dataDir.empty() ? dataDir : dataDir / L"taskbar-restore.json";
}

Win32Taskbar::Win32Taskbar(TaskbarTarget target, std::filesystem::path recordPath, log::ILogger& log)
    : m_target(std::move(target)), m_recordPath(std::move(recordPath)), m_log(log)
{
}

std::vector<TaskbarWindow> Win32Taskbar::FindWindows() const
{
    // Only Explorer's own taskbar: other apps (e.g. tray widgets) create windows with the same classes.
    DWORD shellProcess = 0;
    if (m_target.IsReal())
    {
        const HWND shell = GetShellWindow();
        if (shell == nullptr || GetWindowThreadProcessId(shell, &shellProcess) == 0)
        {
            return {};
        }
    }

    std::vector<TaskbarWindow> windows;
    const auto collect = [&](const std::wstring& windowClass, bool primary) {
        HWND window = nullptr;
        while ((window = FindWindowExW(nullptr, window, windowClass.c_str(), nullptr)) != nullptr)
        {
            DWORD owner = 0;
            GetWindowThreadProcessId(window, &owner);
            if (!m_target.IsReal() || owner == shellProcess)
            {
                windows.push_back({window, primary});
            }
        }
    };
    collect(m_target.PrimaryClass(), true);
    collect(m_target.SecondaryClass(), false);
    return windows;
}

std::uint32_t Win32Taskbar::GetAutoHideState() const
{
    if (m_target.IsReal())
    {
        APPBARDATA data{.cbSize = sizeof(APPBARDATA)};
        return static_cast<std::uint32_t>(SHAppBarMessage(ABM_GETSTATE, &data));
    }
    const auto text = ReadWholeFile(m_target.TestAutoHideFile()).value_or("0");
    std::uint32_t state = 0;
    std::from_chars(text.data(), text.data() + text.size(), state);
    return state;
}

bool Win32Taskbar::SetAutoHideState(std::uint32_t state) const
{
    if (m_target.IsReal())
    {
        const auto windows = FindWindows();
        APPBARDATA data{.cbSize = sizeof(APPBARDATA)};
        data.hWnd = windows.empty() ? nullptr : windows.front().hwnd;
        data.lParam = state;
        // Always returns TRUE, and the change has been seen to revert itself: callers verify with
        // GetAutoHideState.
        SHAppBarMessage(ABM_SETSTATE, &data);
        return true;
    }
    std::ofstream file(m_target.TestAutoHideFile(), std::ios::binary | std::ios::trunc);
    file << state;
    return static_cast<bool>(file);
}

bool Win32Taskbar::AutoHideIs(std::uint32_t state) const
{
    // Since Windows 7, ABM_GETSTATE never reports ABS_ALWAYSONTOP; only the auto-hide bit is comparable.
    return (GetAutoHideState() & ABS_AUTOHIDE) == (state & ABS_AUTOHIDE);
}

int Win32Taskbar::ShowHiddenWindows() const
{
    int shown = 0;
    for (const auto& window : FindWindows())
    {
        // A secondary taskbar whose monitor is gone stays as Explorer left it.
        if (IsWindowVisible(window.hwnd) ||
            (!window.primary && MonitorFromWindow(window.hwnd, MONITOR_DEFAULTTONULL) == nullptr))
        {
            continue;
        }
        if (ShowWindowAsync(window.hwnd, SW_SHOWNA))
        {
            ++shown;
        }
    }
    return shown;
}

RestoreResult Win32Taskbar::Restore(const RestoreOptions& options)
{
    RestoreResult result;
    result.windowsShown = ShowHiddenWindows();
    if (result.windowsShown > 0)
    {
        m_log.Info(std::format("taskbar: showed {} hidden taskbar window(s)", result.windowsShown));
    }

    std::error_code error;
    if (m_recordPath.empty() || !std::filesystem::exists(m_recordPath, error))
    {
        return result;
    }
    const auto text = ReadWholeFile(m_recordPath);
    if (!text)
    {
        m_log.Error("taskbar: can't read the restore record; keeping it to try again");
        result.recordKept = true;
        return result;
    }

    const auto record = taskbar::DecodeRestoreRecord(*text);
    // Records are only written when auto-hide was off, so off is the right answer for a damaged one.
    const std::uint32_t state = record ? record->stateBefore : 0;
    const std::string recorded = record ? " (recorded " + record->createdUtc + ")" : " (damaged record)";

    SetAutoHideState(state);
    bool held = AutoHideIs(state);
    if (held && options.verifyDelay.count() > 0)
    {
        std::this_thread::sleep_for(options.verifyDelay);
        held = AutoHideIs(state);
    }
    if (!held)
    {
        m_log.Error(
            std::format("taskbar: auto-hide didn't go back to {}{}; keeping the restore record to try again",
                        AutoHideName(state), recorded));
        result.recordKept = true;
        return result;
    }

    result.autoHideRestoredTo = state;
    if (options.sessionEnding)
    {
        m_log.Info(
            std::format("taskbar: auto-hide restored to {}{}; the session is ending, so the record stays "
                        "until the next start",
                        AutoHideName(state), recorded));
        result.recordKept = true;
        return result;
    }
    const DWORD deleteError = DeleteFileW(m_recordPath.c_str()) ? ERROR_SUCCESS : GetLastError();
    if (deleteError != ERROR_SUCCESS && deleteError != ERROR_FILE_NOT_FOUND)
    {
        m_log.Error(
            std::format("taskbar: auto-hide restored to {}{}, but the record can't be deleted (error {})",
                        AutoHideName(state), recorded, deleteError));
        result.recordKept = true;
        return result;
    }
    m_log.Info(std::format("taskbar: auto-hide restored to {}{}", AutoHideName(state), recorded));
    return result;
}

} // namespace harbor::adapters
