#pragma once

#include <windows.h>

#include <string>

#include "core/ipc/messages.h"

namespace harbor::adapters
{

enum class LaunchRoute
{
    // Explorer started it (IShellDispatch2::ShellExecute on the desktop's shell view).
    Explorer,
    // The dock started it itself (ShellExecuteEx), after Explorer couldn't.
    Direct,
};

struct LaunchResult
{
    HRESULT result = E_FAIL;
    LaunchRoute route = LaunchRoute::Explorer;
    // Why the Explorer route wasn't taken, with LaunchRoute::Direct.
    HRESULT explorerResult = S_OK;
};

// What gets opened: "shell:AppsFolder\<AUMID>" for an app, the path itself for a path.
std::wstring LaunchTarget(const ipc::DockItem& item);

// Starts an app as if from the Start menu. Explorer itself starts it when it can, so the app isn't the dock's
// child (no inherited job, environment or handles) and may take the foreground; Explorer is checked to be
// responsive first. Otherwise (Explorer not running, hung or refusing, as on some machines) the dock calls
// ShellExecuteEx. Needs COM initialized (STA) on the calling thread, and may block for a while: never call it
// on the UI thread.
LaunchResult LaunchApp(const ipc::DockItem& item) noexcept;

} // namespace harbor::adapters
