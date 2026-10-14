using System.ComponentModel;
using System.Diagnostics;
using System.Threading.Channels;
using Harbor.Core.Ipc;
using Harbor.Core.Logging;
using Harbor.Core.Taskbar;

namespace Harbor.Core.Supervision;

public sealed record DockSupervisorOptions
{
    /// <summary>harbor-dock.exe to start when no dock connects by itself; null to only supervise docks started elsewhere.</summary>
    public string? DockExePath { get; init; }

    /// <summary>Extra arguments for a dock the watchdog starts (tests pass the test-mode options).</summary>
    public IReadOnlyList<string> DockArguments { get; init; } = [];

    public TimeSpan PingInterval { get; init; } = TimeSpan.FromSeconds(1);

    /// <summary>Pings in a row the dock may leave unanswered before it counts as hung.</summary>
    public int MissedPongLimit { get; init; } = 5;

    /// <summary>How long to wait at start for a dock that is already running to reconnect.</summary>
    public TimeSpan AdoptWait { get; init; } = TimeSpan.FromSeconds(3);

    /// <summary>Explorer can block a restore; past this, the watchdog stops waiting for it.</summary>
    public TimeSpan RestoreTimeout { get; init; } = TimeSpan.FromSeconds(5);

    /// <summary>See <see cref="RestoreOptions.VerifyDelay"/>.</summary>
    public TimeSpan RestoreVerifyDelay { get; init; } = TimeSpan.FromSeconds(1);
}

public enum DockState
{
    WaitingForDock,
    Running,
    Stopped,
}

/// <param name="ExitCode">Set once a dock has stopped.</param>
/// <param name="Reason">Why it stopped or isn't running, for people.</param>
public sealed record DockStatus(DockState State, uint? ProcessId = null, int? ExitCode = null, string? Reason = null);

/// <param name="Reason">Why the taskbar was restored, for people.</param>
/// <param name="Result">What was restored; null if Explorer didn't answer in time.</param>
public sealed record TaskbarRestore(string Reason, RestoreResult? Result);

/// <summary>
/// Harbor Shell's watchdog over the dock (CLAUDE.md rule 1). It starts the dock or adopts one that is already
/// running, pings it, and restores the taskbar when the dock ends for any reason or stops answering.
/// <para>Only the dock touches the taskbar's auto-hide setting while it is alive. So while a dock lives, the
/// watchdog only shows the taskbar's windows; it restores auto-hide (from the record) once the process has
/// ended.</para>
/// <para>Everything runs on one loop, fed by a queue: link changes, ticks, process exits and requests from the
/// UI never race each other.</para>
/// </summary>
public sealed class DockSupervisor(DockSupervisorOptions options, ControlPipeServer server, TaskbarRestorer restorer, ILog log)
{
    private readonly Channel<Func<Session, Task>> _commands = Channel.CreateUnbounded<Func<Session, Task>>(
        new UnboundedChannelOptions { SingleReader = true });

    public event Action<DockStatus>? DockStatusChanged;

    /// <summary>The control link's status (connection, last pong, round trip), forwarded for display.</summary>
    public event Action<ControlLinkStatus>? LinkStatusChanged;

    public event Action<TaskbarRestore>? TaskbarRestored;

    /// <summary>
    /// Serves the control pipe and supervises until cancelled. Throws what <see cref="ControlPipeServer.RunAsync"/>
    /// throws if the pipe is taken (another Harbor Shell runs); then nothing was touched.
    /// </summary>
    public async Task RunAsync(CancellationToken cancellationToken)
    {
        server.StatusChanged += OnLinkStatus;
        try
        {
            // The pipe exists, or creating it failed, by the time RunAsync returns.
            var serving = server.RunAsync(cancellationToken);
            if (serving.IsFaulted)
            {
                await serving.ConfigureAwait(false);
            }

            using var session = new Session(options.MissedPongLimit, Stopwatch.GetTimestamp());
            using var stop = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            var ticking = TickAsync(stop.Token);
            var watching = WatchAsync(session, stop.Token);
            try
            {
                // Cancellation ends all three; a failing pipe ends the watch too, below.
                await Task.WhenAny(serving, watching).ConfigureAwait(false);
                await serving.ConfigureAwait(false);
                await watching.ConfigureAwait(false);
            }
            finally
            {
                await stop.CancelAsync().ConfigureAwait(false);
                await IgnoreCancellationAsync(ticking).ConfigureAwait(false);
                await IgnoreCancellationAsync(watching).ConfigureAwait(false);
            }
        }
        finally
        {
            server.StatusChanged -= OnLinkStatus;
        }
    }

    /// <summary>Starts a dock if none is running (the "Start dock" button).</summary>
    public void StartDock() => Post(session => session.Dock is null ? LaunchAsync(session) : Task.CompletedTask);

    /// <summary>
    /// Restores the taskbar now (the "Restore taskbar now" button): only its windows while a dock is alive,
    /// auto-hide too when none is. Null if Explorer didn't answer in time.
    /// </summary>
    public Task<RestoreResult?> RestoreTaskbarAsync()
    {
        var done = new TaskCompletionSource<RestoreResult?>(TaskCreationOptions.RunContinuationsAsynchronously);
        Post(async session =>
        {
            try
            {
                done.SetResult(session.Dock is not null
                    ? new RestoreResult(restorer.ShowHiddenWindows(), null, RecordKept: false, null)
                    : await FullRestoreAsync().ConfigureAwait(false));
            }
            catch (Exception exception)
            {
                done.SetException(exception);
                throw;
            }
        });
        return done.Task;
    }

    private void Post(Func<Session, Task> command) => _commands.Writer.TryWrite(command);

    private void OnLinkStatus(ControlLinkStatus status)
    {
        LinkStatusChanged?.Invoke(status);
        Post(session => HandleLinkStatusAsync(session, status));
    }

    private async Task TickAsync(CancellationToken cancellationToken)
    {
        using var timer = new PeriodicTimer(options.PingInterval);
        while (await timer.WaitForNextTickAsync(cancellationToken).ConfigureAwait(false))
        {
            Post(OnTickAsync);
        }
    }

    private async Task WatchAsync(Session session, CancellationToken cancellationToken)
    {
        await foreach (var command in _commands.Reader.ReadAllAsync(cancellationToken).ConfigureAwait(false))
        {
            try
            {
                await command(session).ConfigureAwait(false);
            }
            catch (Exception exception) when (exception is not OperationCanceledException)
            {
                // One failed step must not end the watch.
                log.Error($"watchdog: {exception}");
            }
        }
    }

    private Task HandleLinkStatusAsync(Session session, ControlLinkStatus status)
    {
        if (!status.Connected)
        {
            session.Heartbeat.Reset();
        }
        else if (status.LastPongSeq is { } seq)
        {
            session.Heartbeat.Answered(seq);
            session.DebuggerReported = false;
        }
        else
        {
            // A new connection.
            session.Heartbeat.Reset();
            if (session.Dock is null && status.ClientProcessId is { } processId && DockProcess.Adopt(processId, log) is { } dock)
            {
                log.Info($"watchdog: supervising dock process {processId}, which was already running");
                Supervise(session, dock);
            }
            else if (session.Dock is not null && status.ClientProcessId != session.Dock.Id)
            {
                log.Warn($"watchdog: process {status.ClientProcessId} connected, not the supervised dock {session.Dock.Id}");
            }
        }
        return Task.CompletedTask;
    }

    private async Task OnTickAsync(Session session)
    {
        if (session.Dock is null)
        {
            if (!session.StartupDone && Stopwatch.GetElapsedTime(session.StartedAt) >= options.AdoptWait)
            {
                session.StartupDone = true;
                await StartupAsync(session).ConfigureAwait(false);
            }
            return;
        }
        if (!server.IsConnected)
        {
            return;
        }
        if (session.Heartbeat.IsHung && HandleHang(session))
        {
            return;
        }
        if (server.TryPing() is { } seq)
        {
            session.Heartbeat.Sent(seq);
        }
    }

    // No dock reconnected: an earlier run may have ended with the taskbar hidden. Put it back, then start one.
    private async Task StartupAsync(Session session)
    {
        if (server.IsConnected)
        {
            // A dock connected that this process can't supervise (see DockProcess.Adopt): leave everything to it.
            DockStatusChanged?.Invoke(new DockStatus(DockState.WaitingForDock, Reason: "a dock is running that Harbor Shell can't supervise"));
            return;
        }
        var result = await FullRestoreAsync().ConfigureAwait(false);
        if (result is null || result.WindowsShown > 0 || result.AutoHideRestoredTo is not null)
        {
            TaskbarRestored?.Invoke(new TaskbarRestore("an earlier Harbor run ended without restoring it", result));
        }
        await LaunchAsync(session).ConfigureAwait(false);
    }

    private Task LaunchAsync(Session session)
    {
        if (options.DockExePath is null || !File.Exists(options.DockExePath))
        {
            DockStatusChanged?.Invoke(new DockStatus(DockState.WaitingForDock, Reason: "harbor-dock.exe wasn't found next to Harbor Shell; start it by hand"));
            return Task.CompletedTask;
        }
        try
        {
            var dock = DockProcess.Launch(options.DockExePath, options.DockArguments, log);
            log.Info($"watchdog: started dock process {dock.Id}");
            Supervise(session, dock);
        }
        catch (Win32Exception exception)
        {
            log.Error($"watchdog: can't start {options.DockExePath}: {exception.Message}");
            DockStatusChanged?.Invoke(new DockStatus(DockState.Stopped, Reason: $"couldn't start: {exception.Message}"));
        }
        return Task.CompletedTask;
    }

    private void Supervise(Session session, DockProcess dock)
    {
        session.Dock = dock;
        session.KillReason = null;
        session.StartupDone = true;
        DockStatusChanged?.Invoke(new DockStatus(DockState.Running, dock.Id));
        _ = dock.Exited.ContinueWith(_ => Post(s => HandleExitAsync(s, dock)), TaskScheduler.Default);
    }

    // True if the dock was ended; false if it is being debugged and keeps running (and keeps being pinged, so
    // the watchdog sees it come back).
    private bool HandleHang(Session session)
    {
        var dock = session.Dock!;
        if (dock.IsDebuggerAttached)
        {
            // Someone is debugging the dock, probably stopped at a breakpoint: give them the taskbar back, but
            // leave the process alone.
            if (!session.DebuggerReported)
            {
                session.DebuggerReported = true;
                log.Warn("watchdog: the dock stopped answering with a debugger attached; showing the taskbar, not ending the dock");
                restorer.ShowHiddenWindows();
            }
            return false;
        }

        log.Error($"watchdog: the dock stopped answering ({session.Heartbeat.Missed} pings in a row); showing the taskbar and ending it");
        // The dock is alive, so only the windows; auto-hide follows once the process is gone.
        var shown = restorer.ShowHiddenWindows();
        TaskbarRestored?.Invoke(new TaskbarRestore("the dock stopped responding", new RestoreResult(shown, null, RecordKept: false, null)));
        session.KillReason = "it stopped responding";
        session.Heartbeat.Reset();
        dock.Kill();
        return true;
    }

    private async Task HandleExitAsync(Session session, DockProcess dock)
    {
        if (session.Dock != dock)
        {
            return;
        }
        var exitCode = await dock.Exited.ConfigureAwait(false);
        session.Dock = null;
        session.Heartbeat.Reset();
        dock.Dispose();

        if (exitCode == DockExitCodes.AlreadyRunning)
        {
            // It found another dock running; that one connects and gets adopted.
            log.Info($"watchdog: dock process {dock.Id} exited because another dock is already running");
            return;
        }
        var reason = session.KillReason ?? (exitCode == 0 ? "it exited" : $"it stopped unexpectedly (exit code 0x{exitCode:X8})");
        log.Warn($"watchdog: dock process {dock.Id} ended: {reason}; restoring the taskbar");
        var result = await FullRestoreAsync().ConfigureAwait(false);
        TaskbarRestored?.Invoke(new TaskbarRestore($"the dock ended: {reason}", result));
        DockStatusChanged?.Invoke(new DockStatus(DockState.Stopped, dock.Id, exitCode, reason));
    }

    private Task<RestoreResult?> FullRestoreAsync() => restorer.RestoreAsync(
        new RestoreOptions { SessionEnding = TaskbarRestorer.IsSessionEnding, VerifyDelay = options.RestoreVerifyDelay },
        options.RestoreTimeout);

    private static async Task IgnoreCancellationAsync(Task task)
    {
        try
        {
            await task.ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
        }
    }

    // The watchdog's state, owned by its loop.
    private sealed class Session(int missedPongLimit, long startedAt) : IDisposable
    {
        public long StartedAt { get; } = startedAt;

        public Heartbeat Heartbeat { get; } = new(missedPongLimit);

        public DockProcess? Dock { get; set; }

        public bool StartupDone { get; set; }

        public bool DebuggerReported { get; set; }

        public string? KillReason { get; set; }

        public void Dispose() => Dock?.Dispose();
    }
}
