namespace Harbor.Core.Supervision;

public sealed record RestartOptions
{
    /// <summary>Waits before the 1st, 2nd, 3rd... restart within <see cref="Window"/>; the last one repeats.</summary>
    public IReadOnlyList<TimeSpan> Delays { get; init; } = [TimeSpan.FromSeconds(1), TimeSpan.FromSeconds(2), TimeSpan.FromSeconds(4)];

    /// <summary>At most this many restarts within <see cref="Window"/>; after that the dock stays stopped.</summary>
    public int MaxRestarts { get; init; } = 3;

    public TimeSpan Window { get; init; } = TimeSpan.FromSeconds(60);
}

/// <param name="Delay">How long to wait before starting the dock again; null: don't.</param>
/// <param name="Reason">Why, for people.</param>
public sealed record RestartDecision(TimeSpan? Delay, string Reason);

/// <summary>
/// Whether the watchdog starts a dock again after it ended (M2's auto-restart). Only after a failure (a crash, a
/// hang the watchdog ended, a dock that never connected), never after the dock exited on purpose or can't run
/// here, never while Windows signs out, and only a few times a minute, so a dock that keeps dying stops.
/// </summary>
public sealed class RestartPolicy(RestartOptions options)
{
    private readonly Queue<TimeSpan> _restarts = new();

    /// <param name="exitCode">The dock's exit code.</param>
    /// <param name="now">Any steadily increasing time (the watchdog's clock).</param>
    /// <param name="sessionEnding">Windows is signing out or shutting down.</param>
    public RestartDecision Decide(int exitCode, TimeSpan now, bool sessionEnding)
    {
        ArgumentNullException.ThrowIfNull(options);
        switch (exitCode)
        {
            case 0:
                return new RestartDecision(null, "it exited normally");
            case DockExitCodes.AlreadyRunning:
                return new RestartDecision(null, "another dock is running");
            case DockExitCodes.InvalidArguments:
                return new RestartDecision(null, "its command line was rejected");
            case DockExitCodes.GraphicsUnavailable:
                return new RestartDecision(null, "it can't draw on this PC (graphics unavailable)");
        }
        if (sessionEnding)
        {
            return new RestartDecision(null, "Windows is signing out");
        }

        while (_restarts.Count > 0 && now - _restarts.Peek() >= options.Window)
        {
            _restarts.Dequeue();
        }
        if (_restarts.Count >= options.MaxRestarts)
        {
            return new RestartDecision(null, $"it stopped {_restarts.Count + 1} times within {options.Window.TotalSeconds:0} s; giving up");
        }
        var delay = options.Delays[Math.Min(_restarts.Count, options.Delays.Count - 1)];
        _restarts.Enqueue(now);
        return new RestartDecision(delay, $"starting it again in {delay.TotalSeconds:0.#} s");
    }
}
