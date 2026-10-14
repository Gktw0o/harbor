// harbor-dock entry point and composition root. Options (for tests only) are in app/command_line.h.

#include <windows.h>
// Before WIL, which enables its COM helpers (unique_couninitialize_call) only when objbase.h is in.
#include <chrono>
#include <format>
#include <objbase.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <wil/resource.h>

#include "adapters/composition_renderer.h"
#include "adapters/data_dir.h"
#include "adapters/file_log.h"
#include "adapters/named_pipe_client.h"
#include "adapters/process_job.h"
#include "adapters/win32_taskbar.h"
#include "app/command_line.h"
#include "app/crash_guard.h"
#include "app/dock_window.h"
#include "app/exit_codes.h"
#include "app/hidden_windows.h"
#include "app/taskbar_guard.h"
#include "core/ipc/pipe_name.h"

namespace
{

namespace exit_code = harbor::dock::exit_code;

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

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int)
{
    const auto arguments = CommandLineArguments();
    const std::vector<std::wstring_view> argumentViews(arguments.begin(), arguments.end());
    const auto parsed = harbor::dock::ParseCommandLine(argumentViews);
    if (const auto* error = std::get_if<std::string>(&parsed))
    {
        // No log file: the data directory itself may be the bad argument.
        harbor::adapters::FileLog({}, L"dock").Error(*error);
        return exit_code::kInvalidArguments;
    }
    const auto& options = std::get<harbor::dock::DockOptions>(parsed);
    const bool testMode = options.testTaskbarId.has_value();

    const auto dataDir = options.dataDir.value_or(harbor::adapters::DefaultDataDir());
    harbor::adapters::FileLog log(dataDir.empty() ? dataDir : dataDir / L"logs", L"dock");

    // Crash handlers first, so they cover everything after. They only act on windows the dock hid.
    static harbor::dock::HiddenWindows hiddenWindows;
    harbor::dock::crash_guard::Install(hiddenWindows);
    // A hung UI thread must not turn the island into a framed "Not Responding" ghost window over everything.
    DisableProcessWindowsGhosting();

    DWORD sessionId = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &sessionId);
    const std::string pipeName =
        options.pipeName.value_or(harbor::ipc::PipeName(harbor::ipc::kControlChannel, sessionId));
    log.Info(std::format("harbor-dock starting: {} build, process {}, control pipe {}{}", kArchitecture,
                         GetCurrentProcessId(), pipeName, testMode ? ", test taskbar" : ""));

    // One dock per session (per test id for test docks): two would fight over the taskbar and its record.
    const std::wstring mutexName =
        testMode ? L"Local\\Harbor.Dock.Test." + *options.testTaskbarId : std::wstring(L"Local\\Harbor.Dock");
    const HANDLE mutex = CreateMutexW(nullptr, FALSE, mutexName.c_str());
    const DWORD mutexError = GetLastError();
    const wil::unique_handle instanceMutex(mutex);
    if (mutexError == ERROR_ALREADY_EXISTS)
    {
        log.Warn("another harbor-dock is already running; exiting");
        return exit_code::kAlreadyRunning;
    }

    // Hear about logoff and shutdown before ordinary apps (and, it's hoped, before Explorer saves its
    // settings), so the taskbar's auto-hide is restored first. 0x3FF is the earliest level an app may use.
    SetProcessShutdownParameters(0x3FF, 0);
    const auto job = harbor::adapters::CurrentProcessJob();
    log.Info(std::format("process job: {}", harbor::adapters::ToString(job)));

    harbor::adapters::Win32Taskbar taskbar(
        testMode ? harbor::adapters::TaskbarTarget::Test(*options.testTaskbarId, dataDir)
                 : harbor::adapters::TaskbarTarget::Real(),
        harbor::adapters::RestoreRecordPath(dataDir), log);
    // Startup recovery: an earlier dock may have died with the taskbar hidden or auto-hide changed.
    taskbar.Restore({.sessionEnding = GetSystemMetrics(SM_SHUTTINGDOWN) != 0});

    harbor::taskbar::GuardPolicyConfig policy;
    if (options.testPingTimeoutMs)
    {
        policy.pingTimeout = std::chrono::milliseconds(*options.testPingTimeoutMs);
    }
    harbor::dock::TaskbarGuard guard(taskbar, hiddenWindows, log,
                                     {
                                         .hotkey = testMode ? harbor::dock::TaskbarGuard::kTestHotkey
                                                            : harbor::dock::TaskbarGuard::kPanicHotkey,
                                         .windowTitle = std::wstring(pipeName.begin(), pipeName.end()),
                                         .policy = policy,
                                     });
    if (!guard.Start())
    {
        // Already logged. No guard means no panic hotkey: don't run.
        return ERROR_GEN_FAILURE;
    }

    // The UI thread is a single-threaded apartment: composition, and later UI Automation, need COM here.
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const wil::unique_couninitialize_call uninitializeCom(SUCCEEDED(comResult));
    // Declared before the window, so it outlives it; torn down after guard.Stop() put the taskbar back.
    harbor::adapters::CompositionRenderer renderer(log);
    if (FAILED(comResult) || !renderer.Initialize())
    {
        // Already logged. Starting again wouldn't help; Harbor Shell restores the taskbar and says why.
        return exit_code::kGraphicsUnavailable;
    }

    harbor::adapters::NamedPipeClient controlChannel(harbor::adapters::PipePath(pipeName));
    // A test dock whose test died without ending it goes away by itself.
    const auto orphanTimeout = testMode
                                   ? std::chrono::milliseconds(options.testOrphanTimeoutMs.value_or(30'000))
                                   : std::chrono::milliseconds::zero();
    harbor::dock::DockWindow window(controlChannel, guard, renderer, log, {.orphanTimeout = orphanTimeout});
    switch (window.Create(instance))
    {
    case harbor::dock::DockWindow::CreateResult::Created:
        break;
    case harbor::dock::DockWindow::CreateResult::GraphicsFailed:
        return exit_code::kGraphicsUnavailable;
    case harbor::dock::DockWindow::CreateResult::WindowFailed: {
        const DWORD error = GetLastError();
        log.Error(std::format("couldn't create the dock window: error {}", error));
        return static_cast<int>(error);
    }
    }

    // Test docks only: signaling "Local\Harbor.Test.<id>.hang-ui" hangs the UI thread (see DockWindow).
    wil::unique_event_nothrow hangEvent;
    wil::unique_threadpool_wait hangWait;
    if (testMode &&
        SUCCEEDED(hangEvent.create(wil::EventOptions::ManualReset,
                                   (L"Local\\Harbor.Test." + *options.testTaskbarId + L".hang-ui").c_str())))
    {
        hangWait.reset(CreateThreadpoolWait(
            [](PTP_CALLBACK_INSTANCE, void* context, PTP_WAIT, TP_WAIT_RESULT) {
                PostMessageW(static_cast<HWND>(context), harbor::dock::DockWindow::kTestHangMessage, 0, 0);
            },
            window.Hwnd(), nullptr));
        if (hangWait)
        {
            SetThreadpoolWait(hangWait.get(), hangEvent.get(), nullptr);
        }
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    // Normal exit: put the taskbar back before the process ends.
    guard.Stop();
    log.Info(std::format("harbor-dock exiting with code {}", message.wParam));
    return static_cast<int>(message.wParam);
}
