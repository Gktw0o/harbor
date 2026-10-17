using System.Runtime.InteropServices;
using System.Runtime.InteropServices.Marshalling;

namespace Harbor.Platform.Interop;

/// <summary>ICompositorDesktopInterop: a Windows.UI.Composition target for a plain Win32 window.</summary>
[GeneratedComInterface]
[Guid("29E691FA-4567-4DCA-B319-D0F207EB6807")]
internal partial interface ICompositorDesktopInterop
{
    [PreserveSig] int CreateDesktopWindowTarget(nint window, int isTopmost, out nint target);

    [PreserveSig] int EnsureOnThread(uint threadId);
}

/// <summary>The Win32 calls the island's window makes.</summary>
internal static unsafe partial class IslandNative
{
    public const uint WsPopup = 0x80000000;
    public const uint WsExTopmost = 0x8;
    public const uint WsExToolWindow = 0x80;
    public const uint WsExNoActivate = 0x08000000;
    public const uint WsExNoRedirectionBitmap = 0x00200000;
    public const int GwlExStyle = -20;
    public const int SwHide = 0;
    public const int SwShowNoActivate = 4;
    public const nint HwndTopmost = -1;
    public const uint SwpNoSize = 0x1;
    public const uint SwpNoMove = 0x2;
    public const uint SwpNoActivate = 0x10;
    public const uint WmActivate = 0x0006;
    public const uint WmClose = 0x0010;
    public const uint WmMouseActivate = 0x0021;
    public const uint WmHotkey = 0x0312;
    public const uint WmPowerBroadcast = 0x0218;
    public const uint WmSettingChange = 0x001A;
    public const nint PbtPowerSettingChange = 0x8013;
    public const int MaNoActivate = 3;
    public const uint ModNoRepeat = 0x4000;
    public const int DwmwaUseHostBackdropBrush = 17;
    public const int DwmwaWindowCornerPreference = 33;
    public const int DwmwcpDoNotRound = 1;

    /// <summary>GUID_ENERGY_SAVER_STATUS: the host backdrop draws black while energy saver is on (docs/adr/0013).</summary>
    public static readonly Guid EnergySaverStatus = new("550e8400-e29b-41d4-a716-446655440000");

    [StructLayout(LayoutKind.Sequential)]
    public struct WndClassEx
    {
        public uint Size;
        public uint Style;
        public delegate* unmanaged<nint, uint, nint, nint, nint> WndProc;
        public int ClassExtra;
        public int WindowExtra;
        public nint Instance;
        public nint Icon;
        public nint Cursor;
        public nint Background;
        public char* MenuName;
        public char* ClassName;
        public nint IconSmall;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct PowerBroadcastSetting
    {
        public Guid PowerSetting;
        public uint DataLength;
        public uint Data;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct Rect
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct MonitorInfo
    {
        public uint Size;
        public Rect Monitor;
        public Rect Work;
        public uint Flags;
    }

    [LibraryImport("user32.dll")]
    public static partial nint MonitorFromPoint(long point, uint flags);

    [LibraryImport("user32.dll", EntryPoint = "GetMonitorInfoW")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool GetMonitorInfo(nint monitor, ref MonitorInfo info);

    [LibraryImport("shcore.dll")]
    public static partial int GetDpiForMonitor(nint monitor, int type, out uint dpiX, out uint dpiY);

    [StructLayout(LayoutKind.Sequential)]
    public struct DispatcherQueueOptions
    {
        public int Size;
        public int ThreadType;
        public int ApartmentType;
    }

    [LibraryImport("user32.dll", EntryPoint = "RegisterClassExW")]
    public static partial ushort RegisterClassEx(in WndClassEx windowClass);

    [LibraryImport("user32.dll", EntryPoint = "CreateWindowExW", StringMarshalling = StringMarshalling.Utf16, SetLastError = true)]
    public static partial nint CreateWindowEx(uint exStyle, string className, string title, uint style, int x, int y, int width, int height, nint parent, nint menu, nint instance, nint param);

    [LibraryImport("user32.dll", EntryPoint = "DefWindowProcW")]
    public static partial nint DefWindowProc(nint window, uint message, nint wParam, nint lParam);

    [LibraryImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool DestroyWindow(nint window);

    [LibraryImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool ShowWindow(nint window, int command);

    [LibraryImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool SetWindowPos(nint window, nint after, int x, int y, int width, int height, uint flags);

    [LibraryImport("user32.dll", EntryPoint = "GetWindowLongPtrW")]
    public static partial nint GetWindowLongPtr(nint window, int index);

    [LibraryImport("user32.dll", EntryPoint = "SetWindowLongPtrW")]
    public static partial nint SetWindowLongPtr(nint window, int index, nint value);

    [LibraryImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool SetForegroundWindow(nint window);

    [LibraryImport("user32.dll")]
    public static partial nint GetForegroundWindow();

    [LibraryImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool RegisterHotKey(nint window, int id, uint modifiers, uint key);

    [LibraryImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool UnregisterHotKey(nint window, int id);

    [LibraryImport("user32.dll")]
    public static partial nint RegisterPowerSettingNotification(nint recipient, in Guid setting, uint flags);

    [LibraryImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool UnregisterPowerSettingNotification(nint notification);

    [LibraryImport("dwmapi.dll")]
    public static partial int DwmSetWindowAttribute(nint window, int attribute, in int value, int size);

    [LibraryImport("kernel32.dll", EntryPoint = "GetModuleHandleW", StringMarshalling = StringMarshalling.Utf16)]
    public static partial nint GetModuleHandle(string? name);

    [LibraryImport("CoreMessaging.dll")]
    public static partial int CreateDispatcherQueueController(DispatcherQueueOptions options, out nint controller);

    [LibraryImport("kernel32.dll")]
    public static partial uint GetCurrentThreadId();
}
