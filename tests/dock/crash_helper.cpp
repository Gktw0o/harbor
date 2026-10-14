// Test helper for crash_guard_tests.cpp, run as its own process. It hides the stand-in taskbar windows that
// the test process created, installs the dock's crash handlers (unless --no-handlers), signals the test,
// waits for the go-ahead, and then crashes the requested way.
//
// Usage: harbor_dock_crash_helper <data-dir> <test-id> <kind> [--no-handlers]
// Kinds: access-violation, access-violation-on-thread, abort, terminate, invalid-parameter, purecall

#include <windows.h>

#include <crtdbg.h>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <string_view>
#include <thread>
#include <werapi.h>
#include <wil/resource.h>

#include "adapters/file_log.h"
#include "adapters/win32_taskbar.h"
#include "app/crash_guard.h"
#include "app/hidden_windows.h"

namespace
{

// A pure virtual call: Base's constructor runs while the object is still a Base.
struct Base
{
    Base()
    {
        CallPure();
    }
    virtual ~Base() = default;
    // Not inlined, so the compiler can't resolve the call statically and must go through the vtable.
    __declspec(noinline) void CallPure()
    {
        Pure();
    }
    virtual void Pure() = 0;
};

struct Derived final : Base
{
    void Pure() override
    {
    }
};

void AccessViolation()
{
    volatile int* nowhere = nullptr;
    *nowhere = 1;
}

int Crash(std::wstring_view kind)
{
    if (kind == L"access-violation")
    {
        AccessViolation();
    }
    else if (kind == L"access-violation-on-thread")
    {
        std::thread(AccessViolation).join();
    }
    else if (kind == L"abort")
    {
        std::abort();
    }
    else if (kind == L"terminate")
    {
        std::terminate();
    }
    else if (kind == L"invalid-parameter")
    {
        std::fclose(nullptr);
    }
    else if (kind == L"purecall")
    {
        Derived object;
    }
    return 3;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc < 4)
    {
        return 2;
    }
    // A crash must never wait on a dialog during a test run.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    WerSetFlags(WER_FAULT_REPORTING_NO_UI);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG);
    _set_abort_behavior(0, _WRITE_ABORT_MSG);

    const std::wstring dataDir = argv[1];
    const std::wstring id = argv[2];
    const std::wstring_view kind = argv[3];
    const bool handlers = !(argc > 4 && std::wstring_view(argv[4]) == L"--no-handlers");

    harbor::adapters::FileLog log({}, L"crash-helper");
    const harbor::adapters::Win32Taskbar taskbar(harbor::adapters::TaskbarTarget::Test(id, dataDir), {}, log);
    static harbor::dock::HiddenWindows hidden;
    for (const auto& window : taskbar.FindWindows())
    {
        ShowWindow(window.hwnd, SW_HIDE);
        hidden.Add(window.hwnd);
    }
    if (handlers)
    {
        harbor::dock::crash_guard::Install(hidden);
    }

    const std::wstring prefix = L"Local\\Harbor.Test." + id;
    const wil::unique_event_nothrow hiddenEvent(
        OpenEventW(EVENT_MODIFY_STATE, FALSE, (prefix + L".hidden").c_str()));
    const wil::unique_event_nothrow crashEvent(OpenEventW(SYNCHRONIZE, FALSE, (prefix + L".crash").c_str()));
    if (!hiddenEvent || !crashEvent)
    {
        return 4;
    }
    hiddenEvent.SetEvent();
    if (WaitForSingleObject(crashEvent.get(), 30'000) != WAIT_OBJECT_0)
    {
        return 5;
    }
    return Crash(kind);
}
