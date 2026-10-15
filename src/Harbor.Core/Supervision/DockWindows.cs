using static Harbor.Core.Interop.NativeMethods;

namespace Harbor.Core.Supervision;

/// <summary>The dock's windows, as another process finds them.</summary>
public static class DockWindows
{
    /// <summary>The island's window class. Must match the dock's DockWindow::kWindowClass.</summary>
    public const string IslandClass = "Harbor.Dock.Island";

    /// <summary>
    /// The window class of the dock's per-monitor hosts, which hold its AppBar strips (docs/adr/0011). Must match
    /// the dock's kHostClass.
    /// </summary>
    public const string HostClass = "Harbor.Dock.Host";

    /// <summary>The island of the dock running as this process, or 0.</summary>
    public static nint FindIsland(uint processId) => Find(processId, IslandClass).FirstOrDefault();

    /// <summary>The windows holding the dock's screen strips (one per monitor), for releasing them after it ends.</summary>
    public static IReadOnlyList<nint> FindAppBars(uint processId) => Find(processId, HostClass);

    private static List<nint> Find(uint processId, string className)
    {
        var found = new List<nint>();
        nint window = 0;
        while ((window = FindWindowEx(0, window, className, null)) != 0)
        {
            _ = GetWindowThreadProcessId(window, out var owner);
            if (owner == processId)
            {
                found.Add(window);
            }
        }
        return found;
    }
}
