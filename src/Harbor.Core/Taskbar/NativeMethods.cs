using System.Runtime.InteropServices;

namespace Harbor.Core.Taskbar;

internal static partial class NativeMethods
{
    public const uint AbmGetState = 4;
    public const uint AbmSetState = 10;
    public const uint AbsAutoHide = 1;
    public const int SwShowNa = 8;
    public const uint MonitorDefaultToNull = 0;
    public const int SmShuttingDown = 0x2000;

    [StructLayout(LayoutKind.Sequential)]
    public struct Rect
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct AppBarData
    {
        public uint Size;
        public nint Window;
        public uint CallbackMessage;
        public uint Edge;
        public Rect Bounds;
        public nint Param;
    }

    [LibraryImport("shell32.dll")]
    public static partial nuint SHAppBarMessage(uint message, ref AppBarData data);

    [LibraryImport("user32.dll", EntryPoint = "FindWindowExW", StringMarshalling = StringMarshalling.Utf16)]
    public static partial nint FindWindowEx(nint parent, nint childAfter, string className, string? windowName);

    [LibraryImport("user32.dll")]
    public static partial nint GetShellWindow();

    [LibraryImport("user32.dll")]
    public static partial uint GetWindowThreadProcessId(nint window, out uint processId);

    [LibraryImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool IsWindowVisible(nint window);

    [LibraryImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool ShowWindowAsync(nint window, int command);

    [LibraryImport("user32.dll")]
    public static partial nint MonitorFromWindow(nint window, uint flags);

    [LibraryImport("user32.dll")]
    public static partial int GetSystemMetrics(int index);
}
