#pragma once

#include <windows.h>

namespace harbor::dock::exit_code
{

// harbor-dock.exe exits with these, or with a Windows error code. Harbor.Core's DockExitCodes mirrors them.

// The command line was rejected (details in the debugger output).
inline constexpr int kInvalidArguments = ERROR_INVALID_PARAMETER;
// Another dock already runs for this session (or this test id). Bit 29 marks an application-defined code.
inline constexpr int kAlreadyRunning = 0x20000001;
// Not used by the dock itself: Harbor Shell's watchdog ends a hung dock with this code.
inline constexpr int kKilledByWatchdog = 0x20000002;
// Composition can't start or can't draw into the dock's window: starting the dock again won't help.
inline constexpr int kGraphicsUnavailable = 0x20000003;

} // namespace harbor::dock::exit_code
