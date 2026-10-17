using System.Diagnostics;
using Harbor.Core.Apps;
using Harbor.Core.Ipc;
using Harbor.Core.Logging;
using Harbor.Core.Media;
using Harbor.Core.Power;
using Harbor.Core.Settings;
using Harbor.Core.Supervision;
using Harbor.Core.Taskbar;

namespace Harbor.Core;

/// <summary>
/// Harbor Shell without its windows (the composition root): the dock's watchdog, the pinned apps from
/// settings.json, and the names and icons of running apps. Windows show what it reports and call its commands;
/// <see cref="RunAsync"/> runs it until cancelled. <see cref="ShellOptions"/> decides whether it is the user's
/// Shell or a test one (its own pipe, data dir and fake taskbar).
/// </summary>
public sealed class ShellHost
{
    private readonly SettingsWatcher _settings;
    private readonly DockItemsPublisher _publisher;
    private readonly AppLookupService _apps;

    public ShellHost(ShellOptions options, ILog log, string dockExePath)
    {
        ArgumentNullException.ThrowIfNull(options);
        Options = options;
        Log = log;
        var dataDir = options.DataDirOrDefault;
        using (var process = Process.GetCurrentProcess())
        {
            PipeName = options.PipeName ?? PipeNames.For(PipeNames.ControlChannel, (uint)process.SessionId);
        }

        var restorer = new TaskbarRestorer(options.Taskbar, RestoreRecord.PathIn(dataDir), log);
        Supervisor = new DockSupervisor(
            new DockSupervisorOptions { DockExePath = dockExePath, DockArguments = options.DockArguments },
            new ControlPipeServer(PipeName, log),
            restorer,
            log);

        // Pinned apps: settings.json -> looked up -> dockItems to whichever dock is connected.
        Settings = new SettingsFile(dataDir, log);
        _settings = new SettingsWatcher(Settings, log);
        var icons = new IconStore(dataDir, log);
        _publisher = new DockItemsPublisher(new ShellPinResolver(icons), log, icons);
        _settings.Changed += _publisher.Update;
        _settings.ProblemChanged += problem => SettingsProblemChanged?.Invoke(problem);
        _publisher.Built += Supervisor.SetDockItems;
        _publisher.Built += items => PinnedChanged?.Invoke(items);

        // Running apps: the dock asks (appLookup), the Shell names them (appInfo), after any new dockItems.
        _apps = new AppLookupService(new ShellAppResolver(icons), log, icons);
        Supervisor.AppLookupReceived += _apps.Lookup;
        Supervisor.LinkStatusChanged += status =>
        {
            if (!status.Connected)
            {
                _apps.Disconnected();
            }
        };
        _publisher.Built += _apps.SetPins;
        _apps.Answered += Supervisor.SendToDock;
        // Pin and unpin from the dock's menu: settings.json changes, and the settings watcher sends the new list.
        Supervisor.PinRequestReceived += request => _ = ApplyPinRequestAsync(request);

        // The island's apps (docs/adr/0016). A test Shell reads only its test list, never the machine's apps.
        Apps = new AppIndex(options.TestMode ? new FileAppSource(Path.Combine(dataDir, FileAppSource.FileName)) : new AppsFolderSource(), log);
    }

    /// <summary>The installed apps the island searches.</summary>
    public AppIndex Apps { get; }

    /// <summary>
    /// The power actions for a Shell: a test Shell only ever gets <see cref="RecordingPowerActions"/>, and the real
    /// ones are not even made for it.
    /// </summary>
    public static IPowerActions PowerActionsFor(ShellOptions options, ILog log, Func<IPowerActions> real)
    {
        ArgumentNullException.ThrowIfNull(options);
        ArgumentNullException.ThrowIfNull(real);
        return options.TestMode ? new RecordingPowerActions(log) : real();
    }

    /// <summary>What is playing, for a Shell: a test Shell never sees (or controls) the machine's media.</summary>
    public static IMediaSource MediaSourceFor(ShellOptions options, Func<IMediaSource> real)
    {
        ArgumentNullException.ThrowIfNull(options);
        ArgumentNullException.ThrowIfNull(real);
        return options.TestMode ? new NoMediaSource() : real();
    }

    /// <summary>
    /// Starts an app chosen in the island: through the dock (Explorer's route, so it isn't the Shell's child), or,
    /// with no dock connected, the shell's own way.
    /// </summary>
    public void Launch(AppTarget target)
    {
        ArgumentNullException.ThrowIfNull(target);
        Supervisor.SendToDockOr(new Launch(target), () =>
        {
            var file = target.Kind == DockItemKind.App ? @"shell:AppsFolder\" + target.Target : target.Target;
            try
            {
                using var process = Process.Start(new ProcessStartInfo(file) { UseShellExecute = true });
                Log.Info("launch: from the island, without a dock");
            }
            catch (Exception exception) when (exception is System.ComponentModel.Win32Exception or InvalidOperationException)
            {
                // Which app is the user's business.
                Log.Warn($"launch: from the island, without a dock, failed: {exception.GetType().Name}");
            }
        });
    }

    public ShellOptions Options { get; }

    public ILog Log { get; }

    /// <summary>The control pipe this Shell serves.</summary>
    public string PipeName { get; }

    public DockSupervisor Supervisor { get; }

    public SettingsFile Settings { get; }

    /// <summary>The pinned apps as sent to the dock. Raised on a background thread.</summary>
    public event Action<DockItems>? PinnedChanged;

    /// <summary>Why settings.json isn't used, or null once it is again. Raised on a background thread.</summary>
    public event Action<string?>? SettingsProblemChanged;

    /// <summary>What came of a pin or unpin the dock asked for. Raised on a background thread.</summary>
    public event Action<PinRequest, SettingsChangeResult>? PinRequestHandled;

    /// <summary>
    /// Runs the watchdog and the background loops until cancelled. Throws <see cref="IOException"/> or
    /// <see cref="UnauthorizedAccessException"/> if another process owns the pipe (a second Shell).
    /// </summary>
    public async Task RunAsync(CancellationToken cancellationToken)
    {
        _ = Apps.RefreshAsync(force: true);
        var loops = new[]
        {
            RunQuietlyAsync("settings", _settings.RunAsync, cancellationToken),
            RunQuietlyAsync("dock items", _publisher.RunAsync, cancellationToken),
            RunQuietlyAsync("apps", _apps.RunAsync, cancellationToken),
        };
        try
        {
            await Supervisor.RunAsync(cancellationToken);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
        }
        await Task.WhenAll(loops);
    }

    // A background loop that ends with the Shell; a failure is logged, not thrown at the windows.
    private async Task RunQuietlyAsync(string name, Func<CancellationToken, Task> run, CancellationToken cancellationToken)
    {
        try
        {
            await Task.Run(() => run(cancellationToken), cancellationToken);
        }
        catch (OperationCanceledException)
        {
        }
        catch (Exception exception)
        {
            Log.Error($"{name}: stopped: {exception}");
        }
    }

    private async Task ApplyPinRequestAsync(PinRequest request)
    {
        var result = await PinChanges.ApplyAsync(Settings, request, Log);
        PinRequestHandled?.Invoke(request, result);
    }
}
