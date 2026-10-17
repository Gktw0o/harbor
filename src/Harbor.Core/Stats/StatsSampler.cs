using Harbor.Core.Logging;

namespace Harbor.Core.Stats;

/// <summary>Port: one reading of the machine's stats. Holds counters open between reads; Dispose lets them go.</summary>
public interface IStatsSource : IDisposable
{
    /// <summary>The stats now; parts that aren't available are left out, never thrown.</summary>
    StatsReading Read();
}

/// <summary>
/// Reads the stats once a second, and only while the island shows them (docs/adr/0016): nothing runs while the
/// island is closed. The source (PDH queries) is let go 30 s after the last reading, so opening and closing the
/// island quickly doesn't reopen it each time.
/// </summary>
public sealed class StatsSampler(Func<IStatsSource> open, ILog log, TimeSpan? interval = null, TimeSpan? closeAfter = null) : IDisposable
{
    private readonly TimeSpan _interval = interval ?? TimeSpan.FromSeconds(1);
    private readonly TimeSpan _closeAfter = closeAfter ?? TimeSpan.FromSeconds(30);
    private readonly Lock _lock = new();
    private IStatsSource? _source;
    private CancellationTokenSource? _running;
    private Timer? _closing;

    /// <summary>The source is open now (for tests and the log).</summary>
    public bool SourceOpen
    {
        get
        {
            lock (_lock)
            {
                return _source is not null;
            }
        }
    }

    /// <summary>Starts reading (one at once, then every interval) until <see cref="Stop"/>; readings arrive on a background thread.</summary>
    public void Start(Action<StatsReading> onReading)
    {
        ArgumentNullException.ThrowIfNull(onReading);
        CancellationTokenSource running;
        lock (_lock)
        {
            _closing?.Dispose();
            _closing = null;
            if (_running is not null)
            {
                return;
            }
            _running = running = new CancellationTokenSource();
        }
        _ = Task.Run(() => RunAsync(onReading, running.Token));
    }

    /// <summary>Stops reading; the source closes a while later unless reading starts again.</summary>
    public void Stop()
    {
        lock (_lock)
        {
            _running?.Cancel();
            _running?.Dispose();
            _running = null;
            _closing?.Dispose();
            _closing = new Timer(_ => Close(), null, _closeAfter, Timeout.InfiniteTimeSpan);
        }
    }

    private async Task RunAsync(Action<StatsReading> onReading, CancellationToken token)
    {
        using var timer = new PeriodicTimer(_interval);
        try
        {
            do
            {
                StatsReading reading;
                lock (_lock)
                {
                    if (token.IsCancellationRequested)
                    {
                        return;
                    }
                    _source ??= open();
                    reading = _source.Read();
                }
                onReading(reading);
            }
            while (await timer.WaitForNextTickAsync(token));
        }
        catch (OperationCanceledException)
        {
        }
        catch (Exception exception) when (exception is not OutOfMemoryException)
        {
            // Stats are a nicety: a failing source stops the readings, never the Shell.
            log.Warn($"stats: reading stopped: {exception.GetType().Name}: {exception.Message}");
        }
    }

    private void Close()
    {
        lock (_lock)
        {
            if (_running is not null)
            {
                return;
            }
            _source?.Dispose();
            _source = null;
        }
    }

    public void Dispose()
    {
        lock (_lock)
        {
            _running?.Cancel();
            _running?.Dispose();
            _running = null;
            _closing?.Dispose();
            _closing = null;
            _source?.Dispose();
            _source = null;
        }
    }
}
