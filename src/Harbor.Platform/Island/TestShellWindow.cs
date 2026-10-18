using Harbor.Platform.Interop;

namespace Harbor.Platform.Island;

/// <summary>
/// A test Shell's main window (docs/adr/0013): off screen, and it must never take the user's focus. Off screen is
/// not enough: as an ordinary window it is in Alt+Tab, and Windows hands it the focus when the window in front
/// goes away (seen: the user's Alt+Tab during a local test run landed on it).
/// </summary>
public static class TestShellWindow
{
    /// <summary>Makes the window one that can't be activated (WS_EX_NOACTIVATE).</summary>
    public static void NeverActivate(nint window)
    {
        var style = IslandNative.GetWindowLongPtr(window, IslandNative.GwlExStyle);
        _ = IslandNative.SetWindowLongPtr(window, IslandNative.GwlExStyle, style | (nint)IslandNative.WsExNoActivate);
    }
}
