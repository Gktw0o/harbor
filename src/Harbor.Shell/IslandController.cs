using Harbor.Core;
using Harbor.Core.Controls;
using Harbor.Core.Ipc;
using Harbor.Core.Island;
using Harbor.Core.Media;
using Harbor.Core.Power;
using Harbor.Core.Stats;
using Harbor.Platform.Controls;
using Harbor.Platform.Island;
using Harbor.Platform.Media;
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
    private readonly IPowerActions _power;
    private readonly StatsSampler _stats;
    private readonly IMediaSource _media;
    private readonly ISystemControls _controls;
    private IslandState? _sent;
    private bool _connected;

    public IslandController(ShellHost host, DispatcherQueue queue)
    {
        _host = host;
        _queue = queue;
        _window = new IslandWindow(host.Log);
        _window.RegisterHotkey();
        // A test Shell's power actions only record what was asked (docs/adr/0016).
        _power = ShellHost.PowerActionsFor(host.Options, host.Log, () => new WindowsPowerActions(host.Log, () => _window.Handle));
        // Read only while the island is open; a test Shell reads the real stats too (reading changes nothing).
        _stats = new StatsSampler(() => new WindowsStatsSource(), host.Log);
        // A test Shell's controls are made up: it never touches the machine's volume, display, radios or theme.
        _controls = ShellHost.ControlsFor(host.Options, host.Log, () => new WindowsSystemControls(host.Log, new WinRtRadioSwitches(host.Log)));
        _controls.Changed += () => _queue.TryEnqueue(ShowControls);
        _panel = new IslandPanel(host.Apps, _power.Capabilities, _controls);
        _panel.SettingsRequested += page =>
        {
            // Closed first, so Settings comes up in front of nothing of Harbor's.
            Change(_model.OnDismiss);
            _controls.OpenSettings(page);
        };
        _panel.PowerRequested += action =>
        {
            // Closed first: locking or sleeping with the island open would leave it open on return.
            Change(_model.OnDismiss);
            _power.Run(action);
        };
        // What is playing: a test Shell never sees (or controls) the machine's media.
        _media = ShellHost.MediaSourceFor(host.Options, () => new GsmtcMediaSource(host.Log));
        _media.Changed += () => _queue.TryEnqueue(ShowMedia);
        _panel.MediaRequested += which => _ = which switch
        {
            < 0 => _media.PreviousAsync(),
            0 => _media.PlayPauseAsync(),
            _ => _media.NextAsync(),
        };
        _ = _media.StartAsync();
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
            // A fresh search, over a list read again if it is old (apps come and go); the stats read while it shows.
            _panel.Reset();
            _ = _host.Apps.RefreshAsync();
            // Each second while open: the stats, and the playback position moving on.
            _stats.Start(reading => _queue.TryEnqueue(() =>
            {
                _panel.ShowStats(reading);
                ShowMedia();
            }));
            _media.ArtworkWanted = true;
            ShowMedia();
            _controls.Watching = true;
            ShowControls();
        }
        else if (!_model.Open && wasOpen)
        {
            _stats.Stop();
            _media.ArtworkWanted = false;
            _controls.Watching = false;
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

    // Only while open: the collapsed notch stays empty (docs/adr/0013).
    private void ShowMedia()
    {
        if (_model.Open)
        {
            _panel.ShowMedia(_media.Current, DateTime.UtcNow);
        }
    }

    private void ShowControls()
    {
        if (_model.Open)
        {
            _panel.ShowControls(_controls.Current);
        }
    }

    public void Dispose()
    {
        _stats.Dispose();
        _media.Dispose();
        _controls.Dispose();
        _window.Dispose();
    }
}
