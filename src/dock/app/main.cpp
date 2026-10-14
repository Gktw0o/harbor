// harbor-dock entry point and composition root. Options (for tests only) are in app/command_line.h.

#include <windows.h>

#include <format>
#include <shellapi.h>
#include <string>
#include <vector>
#include <wil/resource.h>

#include "adapters/data_dir.h"
#include "adapters/file_log.h"
#include "adapters/named_pipe_client.h"
#include "adapters/win32_taskbar.h"
#include "app/command_line.h"
#include "app/dock_window.h"
#include "core/ipc/pipe_name.h"

namespace
{

#if defined(_M_ARM64)
constexpr std::string_view kArchitecture = "arm64";
#elif defined(_M_X64)
constexpr std::string_view kArchitecture = "x64";
#else
#error Harbor builds for x64 and arm64 only.
#endif

// The arguments after the program name.
std::vector<std::wstring> CommandLineArguments()
{
    int argc = 0;
    const wil::unique_hlocal_ptr<PWSTR[]> argv(CommandLineToArgvW(GetCommandLineW(), &argc));
    std::vector<std::wstring> arguments;
    for (int i = 1; argv && i < argc; ++i)
    {
        arguments.emplace_back(argv[i]);
    }
    return arguments;
}

} // namespace

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int showCommand)
{
    const auto arguments = CommandLineArguments();
    const std::vector<std::wstring_view> argumentViews(arguments.begin(), arguments.end());
    const auto parsed = harbor::dock::ParseCommandLine(argumentViews);
    if (const auto* error = std::get_if<std::string>(&parsed))
    {
        // No log file: the data directory itself may be the bad argument.
        harbor::adapters::FileLog({}, L"dock").Error(*error);
        return ERROR_INVALID_PARAMETER;
    }
    const auto& options = std::get<harbor::dock::DockOptions>(parsed);

    const auto dataDir = options.dataDir.value_or(harbor::adapters::DefaultDataDir());
    harbor::adapters::FileLog log(dataDir.empty() ? dataDir : dataDir / L"logs", L"dock");

    DWORD sessionId = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &sessionId);
    const std::string pipeName =
        options.pipeName.value_or(harbor::ipc::PipeName(harbor::ipc::kControlChannel, sessionId));
    log.Info(std::format("harbor-dock starting: {} build, process {}, control pipe {}{}", kArchitecture,
                         GetCurrentProcessId(), pipeName, options.testTaskbarId ? ", test taskbar" : ""));

    harbor::adapters::Win32Taskbar taskbar(
        options.testTaskbarId ? harbor::adapters::TaskbarTarget::Test(*options.testTaskbarId, dataDir)
                              : harbor::adapters::TaskbarTarget::Real(),
        harbor::adapters::RestoreRecordPath(dataDir), log);
    // Startup recovery: an earlier dock may have died with the taskbar hidden or auto-hide changed.
    taskbar.Restore({.sessionEnding = GetSystemMetrics(SM_SHUTTINGDOWN) != 0});

    harbor::adapters::NamedPipeClient controlChannel(harbor::adapters::PipePath(pipeName));
    harbor::dock::DockWindow window(controlChannel, log);
    if (!window.Create(instance, showCommand))
    {
        const DWORD error = GetLastError();
        log.Error(std::format("couldn't create the dock window: error {}", error));
        return static_cast<int>(error);
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    log.Info(std::format("harbor-dock exiting with code {}", message.wParam));
    return static_cast<int>(message.wParam);
}
