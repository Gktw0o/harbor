using System.Numerics;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.Marshalling;
using Harbor.Core.Ipc;
using Harbor.Core.Island;
using Harbor.Core.Logging;
using Harbor.Platform.Interop;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Hosting;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Microsoft.Win32;
using Windows.Graphics;
using WinRT;
using WUC = Windows.UI.Composition;

namespace Harbor.Platform.Island;

/// <summary>
/// The island's window (docs/adr/0013, M4 spike round 3): a plain Win32 popup (no redirection bitmap, tool window,
/// topmost, never activated until it opens) whose glass is drawn by Windows.UI.Composition (the host backdrop
/// and a tint, clipped flat at the top and rounded at the bottom) and whose controls are a WinUI XAML island
/// over it. It shows what it is told (<see cref="Apply"/>) and reports the mouse, clicks, dismissals and its
/// hotkey; <see cref="IslandModel"/> decides. One per process, on the UI thread.
/// </summary>
public sealed unsafe partial class IslandWindow : IDisposable
{
    public const string WindowClass = "Harbor.Island";
    private const int HotkeyId = 1;
    // F22, else F21, else F23 (docs/adr/0015): keys no keyboard has, so nothing else wants them.
    private static readonly uint[] HotkeyCandidates = [0x85, 0x84, 0x86];
    private static IslandWindow? s_instance;
    private static bool s_registered;
    private static nint s_systemQueue;

    private readonly ILog _log;
    private readonly nint _hwnd;
    private readonly WUC.Compositor _compositor;
    private readonly WUC.Desktop.DesktopWindowTarget _target;
    private readonly WUC.ContainerVisual _notch;
    private readonly WUC.SpriteVisual _glass;
    private readonly WUC.SpriteVisual _tint;
    private readonly WUC.RectangleClip _clip;
    private readonly DesktopWindowXamlSource _xaml;
    private readonly Grid _root;
    private readonly Grid _panel;
    private Control? _focusOnOpen;
    private nint _energySaverNotification;
    private bool _energySaver;
    private IslandShape _shape = IslandShape.Hidden;
    private ScreenRect _window = new(0, 0, 0, 0);
    private int _dpi = 96;
    private int _generation;
    private bool _disposed;

    public IslandWindow(ILog log)
    {
        if (s_instance is not null)
        {
            throw new InvalidOperationException("One island per process.");
        }
        _log = log;
        EnsureSystemDispatcherQueue();
        _hwnd = CreatePopup();
        s_instance = this;

        _compositor = new WUC.Compositor();
        _target = CreateTarget(_compositor, _hwnd);
        var root = _compositor.CreateContainerVisual();
        root.RelativeSizeAdjustment = Vector2.One;
        _target.Root = root;
        _notch = _compositor.CreateContainerVisual();
        _glass = _compositor.CreateSpriteVisual();
        _glass.RelativeSizeAdjustment = Vector2.One;
        _glass.Brush = _compositor.CreateHostBackdropBrush();
        _tint = _compositor.CreateSpriteVisual();
        _tint.RelativeSizeAdjustment = Vector2.One;
        _notch.Children.InsertAtTop(_glass);
        _notch.Children.InsertAtTop(_tint);
        _clip = _compositor.CreateRectangleClip();
        _notch.Clip = _clip;
        root.Children.InsertAtTop(_notch);

        // What the open island shows comes from Harbor Shell (SetPanel).
        _panel = new Grid { Visibility = Visibility.Collapsed };
        // A transparent (not null) background: the notch takes the mouse everywhere, even where nothing is drawn.
        _root = new Grid { Background = new SolidColorBrush(Colors.Transparent), Children = { _panel } };
        AutomationProperties.SetName(_root, "Harbor island");
        _root.PointerEntered += (_, _) => PointerChanged?.Invoke(true);
        _root.PointerExited += (_, _) => PointerChanged?.Invoke(false);
        _root.Tapped += (_, _) => Clicked?.Invoke();
        _root.KeyDown += OnKeyDown;
        _xaml = new DesktopWindowXamlSource();
        _xaml.Initialize(Win32Interop.GetWindowIdFromWindow(_hwnd));
        _xaml.Content = _root;

        _energySaverNotification = IslandNative.RegisterPowerSettingNotification(_hwnd, IslandNative.EnergySaverStatus, 0 /* DEVICE_NOTIFY_WINDOW_HANDLE */);
        ApplyAppearance();
    }

    /// <summary>The mouse is on the island (true) or left it (false).</summary>
    public event Action<bool>? PointerChanged;

    /// <summary>A click on the island.</summary>
    public event Action? Clicked;

    /// <summary>Esc, or the island lost the keyboard focus while open.</summary>
    public event Action? Dismissed;

    /// <summary>Its hotkey was pressed (the dock's Start button presses it): Harbor Shell may take the foreground now.</summary>
    public event Action? HotkeyPressed;

    public nint Handle => _hwnd;

    /// <summary>The primary monitor and its DPI: where the island goes while no dock says where it is.</summary>
    public static (ScreenRect Monitor, int Dpi) PrimaryMonitor()
    {
        var monitor = IslandNative.MonitorFromPoint(0, 1 /* MONITOR_DEFAULTTOPRIMARY */);
        var info = new IslandNative.MonitorInfo { Size = (uint)sizeof(IslandNative.MonitorInfo) };
        if (monitor == 0 || !IslandNative.GetMonitorInfo(monitor, ref info))
        {
            return (new ScreenRect(0, 0, 1024, 768), 96);
        }
        var result = IslandNative.GetDpiForMonitor(monitor, 0 /* MDT_EFFECTIVE_DPI */, out var dpiX, out _);
        var dpi = result == 0 && dpiX > 0 ? (int)dpiX : 96;
        return (new ScreenRect(info.Monitor.Left, info.Monitor.Top, info.Monitor.Right, info.Monitor.Bottom), dpi);
    }

    /// <summary>The virtual-key code of the hotkey it registered, or null.</summary>
    public uint? Hotkey { get; private set; }

    /// <summary>What the open island shows, and the control that gets the keyboard focus when it opens.</summary>
    public void SetPanel(UIElement panel, Control? focusOnOpen)
    {
        _panel.Children.Clear();
        _panel.Children.Add(panel);
        _focusOnOpen = focusOnOpen;
    }

    /// <summary>Windows' own Start, as the Start button would open it (WM_SYSCOMMAND/SC_TASKLIST: docs/adr/0015).</summary>
    public void OpenWindowsStart() => IslandNative.PostMessage(_hwnd, IslandNative.WmSysCommand, IslandNative.ScTaskList, 0);

    /// <summary>Registers the first free key of F22, F21 and F23 as the island's hotkey (docs/adr/0015).</summary>
    public uint? RegisterHotkey()
    {
        foreach (var key in HotkeyCandidates)
        {
            if (IslandNative.RegisterHotKey(_hwnd, HotkeyId, IslandNative.ModNoRepeat, key))
            {
                Hotkey = key;
                return key;
            }
        }
        _log.Warn($"island: none of F22, F21 or F23 could be registered ({Marshal.GetLastPInvokeError()}); the Start button opens Windows' Start");
        return null;
    }

    /// <summary>
    /// Shows the island as shape on that monitor, animated in the compositor. The window grows to the new size first
    /// and shrinks after the shape has (the clip's radii can't be animated, so they change at once). Opening takes
    /// the foreground if Windows allows it (after the hotkey or a click) and puts the focus in the search box.
    /// </summary>
    public void Apply(IslandShape shape, ScreenRect monitor, int dpi)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        var target = IslandPlacement.WindowFor(shape, monitor, dpi);
        var from = _shape;
        _dpi = dpi;
        if (shape == IslandShape.Hidden)
        {
            _shape = shape;
            _panel.Visibility = Visibility.Collapsed;
            SetActivatable(false);
            IslandNative.ShowWindow(_hwnd, IslandNative.SwHide);
            return;
        }
        if (shape != IslandShape.Expanded)
        {
            _panel.Visibility = Visibility.Collapsed;
            SetActivatable(false);
        }
        // Another monitor (or the first showing): the window goes there at once.
        var moved = target.Top != _window.Top || (target.Left + (target.Width / 2)) != (_window.Left + (_window.Width / 2));
        var grows = from == IslandShape.Hidden || moved || (target.Width * target.Height) > (_window.Width * _window.Height);
        if (grows)
        {
            Place(target);
        }
        if (from == IslandShape.Hidden)
        {
            // Appearing: no animation from nothing.
            SetShape(target.Width, target.Height, Radius(shape));
            IslandNative.ShowWindow(_hwnd, IslandNative.SwShowNoActivate);
            IslandNative.SetWindowPos(_hwnd, IslandNative.HwndTopmost, 0, 0, 0, 0, IslandNative.SwpNoMove | IslandNative.SwpNoSize | IslandNative.SwpNoActivate);
            Opened(shape);
            _shape = shape;
            return;
        }
        var generation = ++_generation;
        var batch = _compositor.CreateScopedBatch(WUC.CompositionBatchTypes.Animation);
        Animate(target.Width, target.Height, Radius(shape), shape == IslandShape.Expanded ? 220 : 160);
        batch.End();
        batch.Completed += (_, _) =>
        {
            if (generation != _generation || _disposed)
            {
                return;
            }
            if (!grows)
            {
                Place(target);
                SetShape(target.Width, target.Height, Radius(shape));
            }
            Opened(shape);
        };
        _shape = shape;
    }

    private float Radius(IslandShape shape) => (float)(IslandPlacement.SizeOf(shape).CornerRadius * _dpi / 96.0);

    private void Opened(IslandShape shape)
    {
        if (shape != IslandShape.Expanded)
        {
            return;
        }
        _panel.Visibility = Visibility.Visible;
        SetActivatable(true);
        var front = IslandNative.SetForegroundWindow(_hwnd);
        _focusOnOpen?.Focus(FocusState.Programmatic);
        _log.Info(front ? "island: open, with the focus" : "island: open; Windows didn't give it the focus");
    }

    // The window, in physical pixels: the notch stays centred in it while it changes size.
    private void Place(ScreenRect window)
    {
        _window = window;
        IslandNative.SetWindowPos(_hwnd, IslandNative.HwndTopmost, window.Left, window.Top, Math.Max(window.Width, 1), Math.Max(window.Height, 1), IslandNative.SwpNoActivate);
        _xaml.SiteBridge.MoveAndResize(new RectInt32(0, 0, Math.Max(window.Width, 1), Math.Max(window.Height, 1)));
        var current = _notch.Size;
        _notch.Offset = new Vector3(MathF.Round((window.Width - current.X) / 2), 0, 0);
    }

    private void SetShape(int width, int height, float radius)
    {
        _notch.Offset = new Vector3(MathF.Round((_window.Width - width) / 2f), 0, 0);
        _notch.Size = new Vector2(width, height);
        _clip.Left = 0;
        _clip.Top = 0;
        _clip.Right = width;
        _clip.Bottom = height;
        _clip.TopLeftRadius = Vector2.Zero;
        _clip.TopRightRadius = Vector2.Zero;
        _clip.BottomLeftRadius = new Vector2(radius, radius);
        _clip.BottomRightRadius = new Vector2(radius, radius);
    }

    private void Animate(int width, int height, float radius, int milliseconds)
    {
        var easing = _compositor.CreateCubicBezierEasingFunction(new Vector2(0.2f, 0.9f), new Vector2(0.25f, 1f));
        var duration = TimeSpan.FromMilliseconds(milliseconds);
        void Start(WUC.CompositionObject target, string property, WUC.KeyFrameAnimation animation)
        {
            animation.Duration = duration;
            target.StartAnimation(property, animation);
        }
        var offset = _compositor.CreateVector3KeyFrameAnimation();
        offset.InsertKeyFrame(1f, new Vector3(MathF.Round((_window.Width - width) / 2f), 0, 0), easing);
        Start(_notch, "Offset", offset);
        var size = _compositor.CreateVector2KeyFrameAnimation();
        size.InsertKeyFrame(1f, new Vector2(width, height), easing);
        Start(_notch, "Size", size);
        var right = _compositor.CreateScalarKeyFrameAnimation();
        right.InsertKeyFrame(1f, width, easing);
        Start(_clip, "Right", right);
        var bottom = _compositor.CreateScalarKeyFrameAnimation();
        bottom.InsertKeyFrame(1f, height, easing);
        Start(_clip, "Bottom", bottom);
        // Clip radii can't be animated (StartAnimation throws E_INVALIDARG): set at once.
        _clip.BottomLeftRadius = new Vector2(radius, radius);
        _clip.BottomRightRadius = new Vector2(radius, radius);
    }

    // Activatable only while open: the notch and the bubble never take the focus from the user's window.
    private void SetActivatable(bool activatable)
    {
        var style = (uint)IslandNative.GetWindowLongPtr(_hwnd, IslandNative.GwlExStyle);
        var wanted = activatable ? style & ~IslandNative.WsExNoActivate : style | IslandNative.WsExNoActivate;
        if (wanted != style)
        {
            IslandNative.SetWindowLongPtr(_hwnd, IslandNative.GwlExStyle, (nint)wanted);
        }
    }

    private void OnKeyDown(object sender, KeyRoutedEventArgs e)
    {
        if (e.Key == Windows.System.VirtualKey.Escape)
        {
            e.Handled = true;
            Dismissed?.Invoke();
        }
    }

    // Glass only with transparency effects on and energy saver off (docs/adr/0013); else a solid tint, as
    // Windows' own flyouts. Dark or light as the apps are.
    private void ApplyAppearance()
    {
        using var personalize = Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize");
        var transparency = personalize?.GetValue("EnableTransparency") is not int enabled || enabled != 0;
        var dark = personalize?.GetValue("AppsUseLightTheme") is int light && light == 0;
        var glass = transparency && !_energySaver;
        _glass.IsVisible = glass;
        var alpha = (byte)(glass ? 150 : 255);
        _tint.Brush = _compositor.CreateColorBrush(dark ? Windows.UI.Color.FromArgb(alpha, 24, 24, 28) : Windows.UI.Color.FromArgb(alpha, 243, 243, 246));
        _root.RequestedTheme = dark ? ElementTheme.Dark : ElementTheme.Light;
        _log.Info($"island: {(dark ? "dark" : "light")}, {(glass ? "glass" : "solid")}");
    }

    private nint OnMessage(uint message, nint wParam, nint lParam)
    {
        switch (message)
        {
            case IslandNative.WmHotkey when wParam == HotkeyId:
                HotkeyPressed?.Invoke();
                return 0;
            case IslandNative.WmMouseActivate when _shape != IslandShape.Expanded:
                return IslandNative.MaNoActivate;
            case IslandNative.WmActivate when (wParam & 0xFFFF) == 0 && _shape == IslandShape.Expanded:
                // WA_INACTIVE: the focus went elsewhere.
                Dismissed?.Invoke();
                break;
            case IslandNative.WmPowerBroadcast when wParam == IslandNative.PbtPowerSettingChange && lParam != 0:
                var setting = (IslandNative.PowerBroadcastSetting*)lParam;
                if (setting->PowerSetting == IslandNative.EnergySaverStatus && setting->DataLength == sizeof(uint))
                {
                    _energySaver = setting->Data != 0;
                    ApplyAppearance();
                }
                return 1;
            case IslandNative.WmSettingChange:
                ApplyAppearance();
                break;
            case IslandNative.WmClose:
                // Alt+F4 on the open island, or anyone's WM_CLOSE: it closes to the notch, never goes. The Shell
                // ends with its main window.
                Dismissed?.Invoke();
                return 0;
        }
        return IslandNative.DefWindowProc(_hwnd, message, wParam, lParam);
    }

    [UnmanagedCallersOnly]
    private static nint WindowProc(nint window, uint message, nint wParam, nint lParam)
    {
        try
        {
            if (s_instance is { } island && island._hwnd == window)
            {
                return island.OnMessage(message, wParam, lParam);
            }
        }
        catch (Exception exception)
        {
            // Nothing may escape into Windows; the island goes on as it can.
            s_instance?._log.Error($"island: window message 0x{message:X4} failed: {exception}");
        }
        return IslandNative.DefWindowProc(window, message, wParam, lParam);
    }

    private static nint CreatePopup()
    {
        var instance = IslandNative.GetModuleHandle(null);
        if (!s_registered)
        {
            fixed (char* name = WindowClass)
            {
                var windowClass = new IslandNative.WndClassEx
                {
                    Size = (uint)sizeof(IslandNative.WndClassEx),
                    WndProc = &WindowProc,
                    Instance = instance,
                    ClassName = name,
                };
                IslandNative.RegisterClassEx(windowClass);
            }
            s_registered = true;
        }
        var hwnd = IslandNative.CreateWindowEx(
            IslandNative.WsExTopmost | IslandNative.WsExToolWindow | IslandNative.WsExNoActivate | IslandNative.WsExNoRedirectionBitmap,
            WindowClass, "Harbor island", IslandNative.WsPopup, 0, 0, 1, 1, 0, 0, instance, 0);
        if (hwnd == 0)
        {
            throw new InvalidOperationException($"The island's window couldn't be created ({Marshal.GetLastPInvokeError()}).");
        }
        IslandNative.DwmSetWindowAttribute(hwnd, IslandNative.DwmwaUseHostBackdropBrush, 1, sizeof(int));
        IslandNative.DwmSetWindowAttribute(hwnd, IslandNative.DwmwaWindowCornerPreference, IslandNative.DwmwcpDoNotRound, sizeof(int));
        return hwnd;
    }

    private static WUC.Desktop.DesktopWindowTarget CreateTarget(WUC.Compositor compositor, nint hwnd)
    {
        var unknown = ((IWinRTObject)compositor).NativeObject.ThisPtr;
        var interop = (ICompositorDesktopInterop)new StrategyBasedComWrappers().GetOrCreateObjectForComInstance(unknown, CreateObjectFlags.None);
        Marshal.ThrowExceptionForHR(interop.CreateDesktopWindowTarget(hwnd, 0, out var raw));
        var target = WUC.Desktop.DesktopWindowTarget.FromAbi(raw);
        Marshal.Release(raw);
        return target;
    }

    // Windows.UI.Composition wants a system dispatcher queue on its thread; WinUI's own is a different one.
    private static void EnsureSystemDispatcherQueue()
    {
        if (Windows.System.DispatcherQueue.GetForCurrentThread() is not null || s_systemQueue != 0)
        {
            return;
        }
        var options = new IslandNative.DispatcherQueueOptions { Size = sizeof(IslandNative.DispatcherQueueOptions), ThreadType = 2, ApartmentType = 2 };
        Marshal.ThrowExceptionForHR(IslandNative.CreateDispatcherQueueController(options, out s_systemQueue));
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }
        _disposed = true;
        if (Hotkey is not null)
        {
            IslandNative.UnregisterHotKey(_hwnd, HotkeyId);
        }
        if (_energySaverNotification != 0)
        {
            IslandNative.UnregisterPowerSettingNotification(_energySaverNotification);
            _energySaverNotification = 0;
        }
        _xaml.Dispose();
        _target.Dispose();
        _compositor.Dispose();
        IslandNative.DestroyWindow(_hwnd);
        s_instance = null;
    }
}
