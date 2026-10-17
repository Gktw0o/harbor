using Harbor.Core;
using Harbor.Core.Ipc;
using Harbor.Core.Island;
using Harbor.Platform.Island;
using Microsoft.UI.Dispatching;

namespace Harbor.Shell;

// The island (docs/adr/0013): IslandModel decides, IslandWindow shows. It follows the dock's dockState, opens on its
// hotkey (the dock's Start button presses it) and tells the dock with islandState. On the UI thread only.
internal sealed partial class IslandController : IDisposable
{
    private readonly ShellHost _host;
    private readonly DispatcherQueue _queue;
    private readonly IslandModel _model = new();
    private readonly IslandWindow _window;
    private readonly IslandPanel _panel;
    private IslandState? _sent;
    private bool _connected;

    public IslandController(ShellHost host, DispatcherQueue queue)
    {
        _host = host;
        _queue = queue;
        _window = new IslandWindow(host.Log);
        _window.RegisterHotkey();
        _panel = new IslandPanel(host.Apps);
        _window.SetPanel(_panel.Root, _panel.Search);
        // Starting an app or Windows' Start closes the island first, so the app comes up in front of nothing of
        // Harbor's.
        _panel.LaunchRequested += target => Change(() =>
        {
            _model.OnDismiss();
            host.Launch(target);
        });
        _panel.WindowsStartRequested += () =>
        {
            Change(_model.OnDismiss);
            _window.OpenWindowsStart();
        };
        host.Apps.Changed += () => _queue.TryEnqueue(_panel.Refresh);
        _window.PointerChanged += over => Change(() => _model.OnPointer(over));
        _window.Clicked += () => Change(_model.OnClick);
        _window.Dismissed += () => Change(_model.OnDismiss);
        _window.HotkeyPressed += () => Change(_model.OnHotkey);
        host.Supervisor.DockStateReceived += state => _queue.TryEnqueue(() => Change(() => _model.OnDockState(state)));
        host.Supervisor.LinkStatusChanged += status => _queue.TryEnqueue(() =>
        {
            if (status.Connected == _connected)
            {
                return;
            }
            _connected = status.Connected;
            // A new dock knows nothing of the island yet; one that went takes its place along.
            _sent = null;
            Change(() =>
            {
                if (!status.Connected)
                {
                    _model.OnDockState(null);
                }
            });
        });
        Change(() => { });
    }

    private void Change(Action change)
    {
        var wasOpen = _model.Open;
        change();
        if (_model.Open && !wasOpen)
        {
            // A fresh search, over a list read again if it is old (apps come and go).
            _panel.Reset();
            _ = _host.Apps.RefreshAsync();
        }
        var (monitor, dpi) = _model.Dock is { } dock ? (dock.Monitor, dock.Dpi) : IslandWindow.PrimaryMonitor();
        _window.Apply(_model.Shape, monitor, dpi);
        var state = new IslandState(Ready: true, Open: _model.Open, Hotkey: (int?)_window.Hotkey);
        if (_connected && state != _sent)
        {
            _sent = state;
            _host.Supervisor.SendToDock(state);
        }
    }

    public void Dispose() => _window.Dispose();
}
