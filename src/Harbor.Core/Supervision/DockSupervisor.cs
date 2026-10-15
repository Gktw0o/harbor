using System.ComponentModel;
using System.Diagnostics;
using System.Threading.Channels;
using Harbor.Core.Interop;
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

    /// <summary>When a dock that failed is started again (<see cref="RestartPolicy"/>).</summary>
    public RestartOptions Restart { get; init; } = new();

    /// <summary>A started dock that hasn't connected by then counts as failed: it is ended (and maybe restarted).</summary>
    public TimeSpan ConnectTimeout { get; init; } = TimeSpan.FromSeconds(10);

    /// <summary>How long "Stop dock" waits for the dock to close by itself before ending it.</summary>
    public TimeSpan StopTimeout { get; init; } = TimeSpan.FromSeconds(5);
}

public enum DockState
{
    WaitingForDock,
    Running,
    Stopped,
}

/// <param name="ExitCode">Set once a dock has stopped.</param>
/// <param name="Reason">Why it stopped or isn't running, for people.</param>
/// <param name="Next">What happens next after a stop (it starts again in a moment, or it doesn't and why), for people.</param>
public sealed record DockStatus(DockState State, uint? ProcessId = null, int? ExitCode = null, string? Reason = null, string? Next = null);

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

    /// <summary>The dock's latest <see cref="TaskbarState"/>.</summary>
    public event Action<TaskbarState>? TaskbarStateChanged;

    /// <summary>
    /// Whether hiding the taskbar is requested. It turns off by itself after the panic hotkey, when Explorer
    /// keeps showing the taskbar, and when the dock ends or hangs: the user turns it back on.
    /// </summary>
    public event Action<bool>? HideRequestChanged;

    /// <summary>
    /// Serves the control pipe and supervises until cancelled. Throws what <see cref="ControlPipeServer.RunAsync"/>
    /// throws if the pipe is taken (another Harbor Shell runs); then nothing was touched.
    /// </summary>
    public async Task RunAsync(CancellationToken cancellationToken)
    {
        server.StatusChanged += OnLinkStatus;
        server.MessageReceived += OnDockMessage;
        try
        {
            // The pipe exists, or creating it failed, by the time RunAsync returns.
            var serving = server.RunAsync(cancellationToken);
            if (serving.IsFaulted)
            {
                await serving.ConfigureAwait(false);
            }

            using var stop = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            using var session = new Session(options.MissedPongLimit, Stopwatch.GetTimestamp(), new RestartPolicy(options.Restart), stop.Token);
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
            server.MessageReceived -= OnDockMessage;
        }
    }

    /// <summary>Starts a dock if none is running (the "Start dock" button).</summary>
    public void StartDock() => Post(session => session.Dock is null ? LaunchAsync(session) : Task.CompletedTask);

    /// <summary>
    /// Stops the dock (the "Stop dock" button): asks it to close, which puts the taskbar back and frees its strip,
    /// and ends it if it hasn't after <see cref="DockSupervisorOptions.StopTimeout"/>. It isn't started again.
    /// </summary>
    public void StopDock() => Post(session =>
    {
        if (session.Dock is not { } dock)
        {
            return Task.CompletedTask;
        }
        session.StopRequested = true;
        var island = session.Island != 0 ? session.Island : DockWindows.FindIsland(dock.Id);
        log.Info($"watchdog: stopping dock process {dock.Id} as asked");
        if (island == 0 || !NativeMethods.PostMessage(island, NativeMethods.WmClose, 0, 0))
        {
            session.KillReason = "you stopped it";
            dock.Kill();
            return Task.CompletedTask;
        }
        _ = Task.Delay(options.StopTimeout, session.Stopping).ContinueWith(
            _ => Post(s =>
            {
                if (s.Dock == dock)
                {
                    log.Warn($"watchdog: dock process {dock.Id} didn't close when asked; ending it");
                    s.KillReason = "you stopped it (it didn't close by itself)";
                    dock.Kill();
                }
                return Task.CompletedTask;
            }),
            CancellationToken.None, TaskContinuationOptions.OnlyOnRanToCompletion, TaskScheduler.Default);
        return Task.CompletedTask;
    });

    /// <summary>
    /// Asks the dock to hide the taskbar or show it (the toggle). The dock is only asked to hide while the
    /// watchdog holds its process, so it can always restore the taskbar if the dock fails.
    /// </summary>
    public void SetTaskbarHidden(bool hidden) => Post(session =>
    {
        SetHideRequest(session, hidden, hidden ? "you turned hiding on" : "you turned hiding off");
        return Task.CompletedTask;
    });

    /// <summary>
    /// Restores the taskbar now (the "Restore taskbar now" button): tells the dock to stop hiding it and shows
    /// its windows while a dock is alive, restores auto-hide too when none is. Null if Explorer didn't answer
    /// in time.
    /// </summary>
    public Task<RestoreResult?> RestoreTaskbarAsync()
    {
        var done = new TaskCompletionSource<RestoreResult?>(TaskCreationOptions.RunContinuationsAsynchronously);
        Post(async session =>
        {
            try
            {
                // First the request, so the dock's re-hiding doesn't fight the windows being shown.
                SetHideRequest(session, false, "you asked to restore the taskbar");
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

    /// <summary>The apps the dock shows. Sent now if a dock is connected, and to every dock that connects.</summary>
    public void SetDockItems(DockItems items)
    {
        ArgumentNullException.ThrowIfNull(items);
        Post(session =>
        {
            session.DockItems = items;
            if (server.IsConnected)
            {
                server.TrySend(items);
            }
            return Task.CompletedTask;
        });
    }

    private void Post(Func<Session, Task> command) => _commands.Writer.TryWrite(command);

    private void OnLinkStatus(ControlLinkStatus status)
    {
        LinkStatusChanged?.Invoke(status);
        Post(session => HandleLinkStatusAsync(session, status));
    }

    private void OnDockMessage(Message message) => Post(session =>
    {
        if (message is TaskbarState state)
        {
            TaskbarStateChanged?.Invoke(state);
            // The dock blocks hiding until it hears "show" (see GuardPolicy); turning the request off sends that.
            if (state.Blocked is TaskbarBlock.PanicHotkey or TaskbarBlock.ExplorerFighting)
            {
                SetHideRequest(session, false, state.Blocked == TaskbarBlock.PanicHotkey
                    ? "the panic hotkey was pressed"
                    : "Explorer kept showing the taskbar");
            }
        }
        return Task.CompletedTask;
    });

    private void SetHideRequest(Session session, bool hidden, string reason)
    {
        if (session.HideRequested != hidden)
        {
            session.HideRequested = hidden;
            log.Info($"watchdog: hiding the taskbar turned {(hidden ? "on" : "off")}: {reason}");
            HideRequestChanged?.Invoke(hidden);
        }
        SendHideRequest(session);
    }

    // Only a dock whose process the watchdog holds, and that is the one connected, may be asked to hide.
    private void SendHideRequest(Session session)
    {
        var supervised = session.Dock is not null && session.ConnectedProcessId == session.Dock.Id;
        server.TrySend(new SetTaskbar(session.HideRequested && supervised));
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
            session.ConnectedProcessId = null;
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
            session.ConnectedProcessId = status.ClientProcessId;
            if (session.Dock is null && status.ClientProcessId is { } processId && DockProcess.Adopt(processId, log) is { } dock)
            {
                log.Info($"watchdog: supervising dock process {processId}, which was already running");
                Supervise(session, dock);
            }
            else if (session.Dock is not null && status.ClientProcessId != session.Dock.Id)
            {
                log.Warn($"watchdog: process {status.ClientProcessId} connected, not the supervised dock {session.Dock.Id}");
            }
            // The dock creates its island before it connects: remembered, to release its screen strip if it dies.
            if (session.Dock is not null && status.ClientProcessId == session.Dock.Id)
            {
                session.Island = DockWindows.FindIsland(session.Dock.Id);
                session.LaunchedAt = null;
            }
            // Every connection hears the current request (the dock forgets it when a connection drops).
            SendHideRequest(session);
            if (session.DockItems is { } items)
            {
                server.TrySend(items);
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
        // A dock this watchdog started that never connected is as good as crashed: it can't be watched.
        if (session.LaunchedAt is { } launchedAt && Stopwatch.GetElapsedTime(launchedAt) >= options.ConnectTimeout)
        {
            log.Error($"watchdog: dock process {session.Dock.Id} didn't connect within {options.ConnectTimeout.TotalSeconds:0} s; ending it");
            session.LaunchedAt = null;
            session.KillReason = "it never connected to Harbor Shell";
            session.Dock.Kill();
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
            session.LaunchedAt = Stopwatch.GetTimestamp();
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
        session.StopRequested = false;
        session.LaunchedAt = null;
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
                SetHideRequest(session, false, "the dock stopped answering (debugger attached)");
                restorer.ShowHiddenWindows();
            }
            return false;
        }

        log.Error($"watchdog: the dock stopped answering ({session.Heartbeat.Missed} pings in a row); showing the taskbar and ending it");
        // The dock's guard thread may still be working (only its UI thread may hang): tell it first.
        SetHideRequest(session, false, "the dock stopped responding");
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
        var stopped = session.StopRequested;
        var reason = session.KillReason ?? (stopped ? "you stopped it"
            : exitCode == 0 ? "it exited"
            : exitCode == DockExitCodes.GraphicsUnavailable ? "it can't draw on this PC (graphics unavailable)"
            : $"it stopped unexpectedly (exit code 0x{exitCode:X8})");
        // After any end, hiding stays off until the user turns it back on.
        SetHideRequest(session, false, $"the dock ended ({reason})");
        log.Warn($"watchdog: dock process {dock.Id} ended: {reason}; restoring the taskbar");
        var result = await FullRestoreAsync().ConfigureAwait(false);
        TaskbarRestored?.Invoke(new TaskbarRestore($"the dock ended: {reason}", result));
        await ReleaseAppBarAsync(session).ConfigureAwait(false);

        // A dock the user stopped stays stopped; one that failed may start again (RestartPolicy).
        var restart = stopped
            ? new RestartDecision(null, "it stays stopped until you start it")
            : session.Restarts.Decide(exitCode, Stopwatch.GetElapsedTime(session.StartedAt), TaskbarRestorer.IsSessionEnding);
        if (restart.Delay is { } delay)
        {
            log.Info($"watchdog: {restart.Reason}");
            _ = Task.Delay(delay, session.Stopping).ContinueWith(
                _ => Post(s => s.Dock is null ? LaunchAsync(s) : Task.CompletedTask),
                CancellationToken.None, TaskContinuationOptions.OnlyOnRanToCompletion, TaskScheduler.Default);
        }
        else if (!stopped && exitCode != 0)
        {
            log.Warn($"watchdog: not starting the dock again: {restart.Reason}");
        }
        DockStatusChanged?.Invoke(new DockStatus(DockState.Stopped, dock.Id, exitCode, reason, restart.Reason));
    }

    // The taskbar first, then the strip: Explorer frees a dead window's strip by itself, so this is only a backup.
    private async Task ReleaseAppBarAsync(Session session)
    {
        var island = session.Island;
        session.Island = 0;
        try
        {
            if (await Task.Run(() => restorer.ReleaseAppBar(island)).WaitAsync(options.RestoreTimeout).ConfigureAwait(false))
            {
                log.Info("watchdog: released the dock's screen strip");
            }
        }
        catch (TimeoutException)
        {
            log.Warn("watchdog: Explorer didn't answer while releasing the dock's screen strip");
        }
    }

    // Explorer has been seen to ignore or revert the change: an unverified restore is tried again a few times
    // (as the dock does). Not while the session ends: there the record stays on purpose.
    private async Task<RestoreResult?> FullRestoreAsync()
    {
        const int Attempts = 3;
        for (var attempt = 1; ; attempt++)
        {
            var sessionEnding = TaskbarRestorer.IsSessionEnding;
            var result = await restorer.RestoreAsync(
                new RestoreOptions { SessionEnding = sessionEnding, VerifyDelay = options.RestoreVerifyDelay },
                options.RestoreTimeout).ConfigureAwait(false);
            var unverified = result is { RecordKept: true, AutoHideRestoredTo: null };
            if (sessionEnding || !unverified || attempt == Attempts)
            {
                return result;
            }
            await Task.Delay(TimeSpan.FromSeconds(1)).ConfigureAwait(false);
        }
    }

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
    private sealed class Session(int missedPongLimit, long startedAt, RestartPolicy restarts, CancellationToken stopping) : IDisposable
    {
        public long StartedAt { get; } = startedAt;

        public RestartPolicy Restarts { get; } = restarts;

        /// <summary>Cancelled when the watchdog stops: delayed work (restarts, stop timeouts) is dropped.</summary>
        public CancellationToken Stopping { get; } = stopping;

        /// <summary>"Stop dock" was asked for this dock: its end is no failure.</summary>
        public bool StopRequested { get; set; }

        /// <summary>When this watchdog started the dock, until it connects (<see cref="DockSupervisorOptions.ConnectTimeout"/>).</summary>
        public long? LaunchedAt { get; set; }

        public Heartbeat Heartbeat { get; } = new(missedPongLimit);

        public DockProcess? Dock { get; set; }

        public bool StartupDone { get; set; }

        public bool DebuggerReported { get; set; }

        public bool HideRequested { get; set; }

        public uint? ConnectedProcessId { get; set; }

        public string? KillReason { get; set; }

        public DockItems? DockItems { get; set; }

        /// <summary>The supervised dock's island window, once it connected.</summary>
        public nint Island { get; set; }

        public void Dispose() => Dock?.Dispose();
    }
}
