using System.Runtime.InteropServices;

namespace Harbor.Core.Tests;

// The dock's island window, as other processes see it.
internal static partial class IslandWindow
{
    public const string ClassName = "Harbor.Dock.Island";
    public const long ExToolWindow = 0x80;
    public const long ExTopmost = 0x8;
    public const long ExNoActivate = 0x08000000;
    public const long ExNoRedirectionBitmap = 0x00200000;

    [StructLayout(LayoutKind.Sequential)]
    public struct Rect
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;

        public readonly int Width => Right - Left;

        public readonly int Height => Bottom - Top;

        public override readonly string ToString() => $"({Left},{Top})-({Right},{Bottom})";
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct MonitorInfo
    {
        public uint Size;
        public Rect Monitor;
        public Rect Work;
        public uint Flags;
    }

    /// <summary>The island of the dock with this process id, or 0.</summary>
    public static nint Find(int processId)
    {
        nint window = 0;
        while ((window = FindWindowEx(0, window, ClassName, null)) != 0)
        {
            _ = GetWindowThreadProcessId(window, out var owner);
            if (owner == processId)
            {
                return window;
            }
        }
        return 0;
    }

    public static Rect Bounds(nint window)
    {
        Assert.IsTrue(GetWindowRect(window, out var rect));
        return rect;
    }

    public static long ExStyle(nint window) => GetWindowLongPtr(window, -20);

    public static (Rect Monitor, Rect Work) PrimaryMonitor()
    {
        var info = new MonitorInfo { Size = (uint)Marshal.SizeOf<MonitorInfo>() };
        Assert.IsTrue(GetMonitorInfo(MonitorFromPoint(0, 0, 1), ref info));
        return (info.Monitor, info.Work);
    }

    [LibraryImport("user32.dll", EntryPoint = "FindWindowExW", StringMarshalling = StringMarshalling.Utf16)]
    private static partial nint FindWindowEx(nint parent, nint childAfter, string className, string? windowName);

    [LibraryImport("user32.dll")]
    private static partial uint GetWindowThreadProcessId(nint window, out int processId);

    [LibraryImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool IsWindowVisible(nint window);

    [LibraryImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool GetWindowRect(nint window, out Rect rect);

    [LibraryImport("user32.dll", EntryPoint = "GetWindowLongPtrW")]
    private static partial nint GetWindowLongPtr(nint window, int index);

    [LibraryImport("user32.dll")]
    private static partial nint MonitorFromPoint(int x, int y, uint flags);

    [LibraryImport("user32.dll", EntryPoint = "GetMonitorInfoW")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool GetMonitorInfo(nint monitor, ref MonitorInfo info);

    [LibraryImport("user32.dll")]
    public static partial nint GetForegroundWindow();
}
