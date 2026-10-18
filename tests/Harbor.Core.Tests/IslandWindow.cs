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

    public const string LabelClassName = "Harbor.Dock.Label";
    public const long ExLayered = 0x80000;
    public const long ExTransparent = 0x20;

    /// <summary>The island of the dock with this process id, or 0.</summary>
    public static nint Find(int processId) => Find(processId, ClassName);

    /// <summary>The name label of the dock with this process id, or 0.</summary>
    public static nint FindLabel(int processId) => Find(processId, LabelClassName);

    /// <summary>Harbor Shell's island (the top one, docs/adr/0013) in the Shell with this process id, or 0.</summary>
    public static nint FindShellIsland(int processId) => Find(processId, "Harbor.Island");

    private static nint Find(int processId, string className)
    {
        nint window = 0;
        while ((window = FindWindowEx(0, window, className, null)) != 0)
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

    /// <summary>A left click at a point of the window's client area, as the window receives it (no real input).</summary>
    public static void Click(nint window, int x, int y)
    {
        const uint ButtonUp = 0x0202;
        Assert.IsTrue(PostMessage(window, ButtonUp, 0, (y << 16) | (x & 0xFFFF)));
    }

    public static void Close(nint window) => Assert.IsTrue(PostMessage(window, 0x0010 /* WM_CLOSE */, 0, 0));

    [LibraryImport("user32.dll", EntryPoint = "PostMessageW")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool PostMessage(nint window, uint message, nuint wParam, nint lParam);

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

    // The process of the window in front ("" if none), e.g. "StartMenuExperienceHost" while Windows' Start is open.
    public static string ForegroundProcessName()
    {
        var window = GetForegroundWindow();
        if (window == 0 || GetWindowThreadProcessId(window, out var processId) == 0)
        {
            return "";
        }
        try
        {
            using var process = System.Diagnostics.Process.GetProcessById(processId);
            return process.ProcessName;
        }
        catch (ArgumentException)
        {
            return "";
        }
    }

    // Presses and releases Esc (CI and foreground tests only: it goes to whatever is in front).
    public static void PressEscape() => Press(0x1B);

    public static void PressEnter() => Press(0x0D);

    // Types lowercase letters and spaces (their virtual keys without Shift), into whatever is in front.
    public static void Type(string text)
    {
        foreach (var c in text)
        {
            Press(c == ' ' ? (byte)0x20 : (byte)char.ToUpperInvariant(c));
        }
    }

    private static void Press(byte key)
    {
        const uint KeyUp = 0x2;
        KeybdEvent(key, 0, 0, 0);
        KeybdEvent(key, 0, KeyUp, 0);
    }

    [LibraryImport("user32.dll", EntryPoint = "keybd_event")]
    private static partial void KeybdEvent(byte key, byte scan, uint flags, nuint extraInfo);
}
