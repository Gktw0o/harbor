using System.Diagnostics;
using System.Globalization;
using System.Runtime.InteropServices;
using Harbor.Core.Taskbar;

namespace Harbor.Core.Tests;

// Same as tests/dock/fake_taskbar.h. Stand-in taskbar windows for TaskbarTarget.Test, so tests never touch
// the real taskbar. They belong to a thread of the test process that pumps messages, because
// ShowWindowAsync (from this or any other process) is carried out by the window's own thread. They are 1x1
// and fully transparent: nothing shows on screen, yet they count as visible.
internal sealed partial class FakeTaskbar : IDisposable
{
    private const uint WsPopup = 0x80000000;
    private const uint WsExToolWindow = 0x80, WsExNoActivate = 0x08000000, WsExLayered = 0x80000, WsExTransparent = 0x20;
    private const uint LwaAlpha = 0x2;
    private const int SwHide = 0, SwShowNa = 8;
    private const uint WmQuit = 0x12;

    private readonly Thread _thread;
    private readonly List<nint> _secondaries = [];
    private readonly List<nint> _offscreen = [];
    private uint _threadId;

    public FakeTaskbar(string dataDir, int secondaries = 1, int offscreenSecondaries = 0)
    {
        Id = Guid.NewGuid().ToString("N");
        Target = TaskbarTarget.Test(Id, dataDir);
        using var ready = new ManualResetEventSlim();
        _thread = new Thread(() => Run(secondaries, offscreenSecondaries, ready)) { IsBackground = true, Name = "FakeTaskbar" };
        _thread.Start();
        ready.Wait();
        if (Primary == 0)
        {
            throw new InvalidOperationException("couldn't create the fake taskbar windows");
        }
    }

    public string Id { get; }

    public TaskbarTarget Target { get; }

    public nint Primary { get; private set; }

    public IReadOnlyList<nint> Secondaries => _secondaries;

    // Secondary taskbars outside every monitor, like one whose display was unplugged.
    public IReadOnlyList<nint> OffscreenSecondaries => _offscreen;

    public string AutoHideFile => Target.TestAutoHideFile!;

    public uint AutoHide
    {
        get
        {
            if (!File.Exists(AutoHideFile))
            {
                return 0;
            }
            // Shared, so polling it never makes the dock's write fail.
            using var reader = new StreamReader(new FileStream(
                AutoHideFile, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete));
            return uint.TryParse(reader.ReadToEnd(), NumberStyles.None, CultureInfo.InvariantCulture, out var state) ? state : 0;
        }
        set => File.WriteAllText(AutoHideFile, value.ToString(CultureInfo.InvariantCulture));
    }

    // Hides a window the way Harbor will: from another thread.
    public static void Hide(nint window) => ShowWindow(window, SwHide);

    // Shows a window the way Explorer shows its taskbar for a peek (docs/adr/0012).
    public static void Show(nint window) => ShowWindow(window, SwShowNa);

    // Moves a window: off the screen, as Explorer slides an auto-hidden taskbar away, or back.
    public static void Move(nint window, int x, int y)
    {
        const uint NoSize = 0x0001, NoZOrder = 0x0004, NoActivate = 0x0010;
        SetWindowPos(window, 0, x, y, 0, 0, NoSize | NoZOrder | NoActivate);
    }

    public static bool IsVisible(nint window) => IsWindowVisible(window);

    // ShowWindowAsync completes on the window's thread, so poll.
    public static bool WaitUntilVisible(nint window, bool visible, TimeSpan? timeout = null)
    {
        var deadline = Stopwatch.StartNew();
        while (IsWindowVisible(window) != visible)
        {
            if (deadline.Elapsed > (timeout ?? TimeSpan.FromSeconds(5)))
            {
                return false;
            }
            Thread.Sleep(10);
        }
        return true;
    }

    public void Dispose()
    {
        PostThreadMessage(_threadId, WmQuit, 0, 0);
        _thread.Join();
        var instance = GetModuleHandle(null);
        UnregisterClass(Target.PrimaryClass, instance);
        UnregisterClass(Target.SecondaryClass, instance);
    }

    private void Run(int secondaries, int offscreenSecondaries, ManualResetEventSlim ready)
    {
        _threadId = GetCurrentThreadId();
        var instance = GetModuleHandle(null);
        var defaultProcedure = NativeLibrary.GetExport(NativeLibrary.Load("user32.dll"), "DefWindowProcW");
        foreach (var windowClass in new[] { Target.PrimaryClass, Target.SecondaryClass })
        {
            var name = Marshal.StringToHGlobalUni(windowClass);
            var description = new WindowClass
            {
                Size = (uint)Marshal.SizeOf<WindowClass>(),
                Procedure = defaultProcedure,
                Instance = instance,
                ClassName = name,
            };
            RegisterClassEx(in description);
            Marshal.FreeHGlobal(name);
        }

        Primary = CreateFake(Target.PrimaryClass, 0, instance);
        for (var i = 0; i < secondaries; i++)
        {
            _secondaries.Add(CreateFake(Target.SecondaryClass, 0, instance));
        }
        for (var i = 0; i < offscreenSecondaries; i++)
        {
            _offscreen.Add(CreateFake(Target.SecondaryClass, -30000, instance));
        }
        ready.Set();

        while (GetMessage(out var message, 0, 0, 0) > 0)
        {
            DispatchMessage(in message);
        }
        foreach (var window in _secondaries.Concat(_offscreen).Prepend(Primary))
        {
            DestroyWindow(window);
        }
    }

    private static nint CreateFake(string windowClass, int position, nint instance)
    {
        var window = CreateWindowEx(
            WsExToolWindow | WsExNoActivate | WsExLayered | WsExTransparent, windowClass, "", WsPopup,
            position, position, 1, 1, 0, 0, instance, 0);
        SetLayeredWindowAttributes(window, 0, 0, LwaAlpha);
        ShowWindow(window, SwShowNa);
        return window;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct WindowClass
    {
        public uint Size;
        public uint Style;
        public nint Procedure;
        public int ClassExtra;
        public int WindowExtra;
        public nint Instance;
        public nint Icon;
        public nint Cursor;
        public nint Background;
        public nint MenuName;
        public nint ClassName;
        public nint SmallIcon;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct Message
    {
        public nint Window;
        public uint Id;
        public nuint WParam;
        public nint LParam;
        public uint Time;
        public int X;
        public int Y;
        public uint Private;
    }

    [LibraryImport("user32.dll", EntryPoint = "RegisterClassExW")]
    private static partial ushort RegisterClassEx(in WindowClass windowClass);

    [LibraryImport("user32.dll", EntryPoint = "UnregisterClassW", StringMarshalling = StringMarshalling.Utf16)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool UnregisterClass(string className, nint instance);

    [LibraryImport("user32.dll", EntryPoint = "CreateWindowExW", StringMarshalling = StringMarshalling.Utf16)]
    private static partial nint CreateWindowEx(
        uint exStyle, string className, string windowName, uint style, int x, int y, int width, int height,
        nint parent, nint menu, nint instance, nint param);

    [LibraryImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool SetLayeredWindowAttributes(nint window, uint key, byte alpha, uint flags);

    [LibraryImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool ShowWindow(nint window, int command);

    [LibraryImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool IsWindowVisible(nint window);

    [LibraryImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool SetWindowPos(nint window, nint insertAfter, int x, int y, int width, int height, uint flags);

    [LibraryImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool DestroyWindow(nint window);

    [LibraryImport("user32.dll", EntryPoint = "GetMessageW")]
    private static partial int GetMessage(out Message message, nint window, uint filterMin, uint filterMax);

    [LibraryImport("user32.dll", EntryPoint = "DispatchMessageW")]
    private static partial nint DispatchMessage(in Message message);

    [LibraryImport("user32.dll", EntryPoint = "PostThreadMessageW")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool PostThreadMessage(uint thread, uint message, nuint wParam, nint lParam);

    [LibraryImport("kernel32.dll")]
    private static partial uint GetCurrentThreadId();

    [LibraryImport("kernel32.dll", EntryPoint = "GetModuleHandleW", StringMarshalling = StringMarshalling.Utf16)]
    private static partial nint GetModuleHandle(string? moduleName);
}
