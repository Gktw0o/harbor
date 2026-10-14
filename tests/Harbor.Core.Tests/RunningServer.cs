using System.Collections.Concurrent;
using System.Diagnostics;
using System.Threading.Channels;
using Harbor.Core.Ipc;

namespace Harbor.Core.Tests;

// A ControlPipeServer running in the background, with its status updates and log collected for assertions.
internal sealed class RunningServer : IAsyncDisposable
{
    public static readonly TimeSpan Timeout = TimeSpan.FromSeconds(15);

    private readonly CancellationTokenSource _stop = new();
    private readonly Channel<ControlLinkStatus> _statuses = Channel.CreateUnbounded<ControlLinkStatus>();

    public RunningServer(string pipeName, TimeSpan pingInterval)
    {
        PipeName = pipeName;
        var server = new ControlPipeServer(pipeName, pingInterval, Log.Enqueue);
        server.StatusChanged += status => _statuses.Writer.TryWrite(status);
        // The pipe exists once this returns: RunAsync creates it before its first await.
        Run = server.RunAsync(_stop.Token);
    }

    public string PipeName { get; }

    public Task Run { get; }

    public ConcurrentQueue<string> Log { get; } = new();

    public static string UniquePipeName() => $"harbor.test.{Guid.NewGuid():N}";

    public async Task<ControlLinkStatus> WaitForAsync(Func<ControlLinkStatus, bool> predicate)
    {
        using var timeout = new CancellationTokenSource(Timeout);
        await foreach (var status in _statuses.Reader.ReadAllAsync(timeout.Token))
        {
            if (predicate(status))
            {
                return status;
            }
        }
        throw new UnreachableException();
    }

    public async ValueTask DisposeAsync()
    {
        await _stop.CancelAsync();
        try
        {
            await Run;
        }
        catch (OperationCanceledException)
        {
        }
        _stop.Dispose();
    }
}
