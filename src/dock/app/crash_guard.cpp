#include "app/crash_guard.h"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <intrin.h>

namespace harbor::dock::crash_guard
{
namespace
{

std::atomic<const HiddenWindows*> g_hidden{nullptr};
std::atomic<bool> g_dying{false};

void RestoreForCrash() noexcept
{
    g_dying.store(true);
    if (const auto* hidden = g_hidden.load())
    {
        hidden->ForEach([](HWND window) { ShowWindowAsync(window, SW_SHOWNA); });
    }
}

LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS*)
{
    RestoreForCrash();
    // Carry on to Windows Error Reporting, which ends the process.
    return EXCEPTION_CONTINUE_SEARCH;
}

void __cdecl OnAbort(int)
{
    // abort() (and std::terminate, which calls it) ends the process once this returns.
    RestoreForCrash();
}

void __cdecl OnInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
{
    RestoreForCrash();
    // Returning would let the failed CRT call carry on.
    __fastfail(FAST_FAIL_INVALID_ARG);
}

void __cdecl OnPureCall()
{
    RestoreForCrash();
    __fastfail(FAST_FAIL_FATAL_APP_EXIT);
}

} // namespace

void Install(const HiddenWindows& hidden) noexcept
{
    g_hidden.store(&hidden);
    SetUnhandledExceptionFilter(&OnUnhandledException);
    std::signal(SIGABRT, &OnAbort);
    _set_invalid_parameter_handler(&OnInvalidParameter);
    _set_purecall_handler(&OnPureCall);
}

bool IsDying() noexcept
{
    return g_dying.load();
}

} // namespace harbor::dock::crash_guard
