using static Harbor.Core.Interop.NativeMethods;

namespace Harbor.Core.Supervision;

/// <summary>The dock's windows, as another process finds them.</summary>
public static class DockWindows
{
    /// <summary>The island's window class. Must match the dock's DockWindow::kWindowClass.</summary>
    public const string IslandClass = "Harbor.Dock.Island";

    /// <summary>The island of the dock running as this process, or 0.</summary>
    public static nint FindIsland(uint processId)
    {
        nint window = 0;
        while ((window = FindWindowEx(0, window, IslandClass, null)) != 0)
        {
            _ = GetWindowThreadProcessId(window, out var owner);
            if (owner == processId)
            {
                return window;
            }
        }
        return 0;
    }
}
