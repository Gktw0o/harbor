using System.Diagnostics;
using Harbor.Core.Apps;
using Harbor.Core.Ipc;
using Harbor.Core.Logging;
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
