#pragma once

#include "app/hidden_windows.h"

namespace harbor::dock::crash_guard
{

// Installs process-wide handlers that, when the dock crashes, mark it as dying and show the taskbar windows
// in `hidden` again (ShowWindowAsync only: no locks, no allocation, no call that can block on Explorer).
// The crash then continues, so Harbor Shell's watchdog and Windows Error Reporting still see it; the Shell
// puts auto-hide back once the process is gone.
//
// Covered: access violations and other SEH exceptions on any thread, abort() and std::terminate (SIGABRT),
// invalid CRT parameters, pure virtual calls. Not covered, because no handler runs at all: fail-fast exits
// (CFG and /GS violations, heap corruption), most stack overflows, and TerminateProcess. For those, and
// whenever a debugger is attached, the Shell's watchdog is the only restore.
//
// `hidden` must live until the process exits.
void Install(const HiddenWindows& hidden) noexcept;

// True once a crash handler has started: from then on nothing may hide the taskbar.
bool IsDying() noexcept;

} // namespace harbor::dock::crash_guard
