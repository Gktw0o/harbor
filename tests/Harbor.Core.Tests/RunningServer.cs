using System.Diagnostics;
using System.Threading.Channels;
using Harbor.Core.Ipc;

namespace Harbor.Core.Tests;

// A ControlPipeServer running in the background and pinging at a fixed interval (as the watchdog does),
// with its status updates and log collected for assertions.
internal sealed class RunningServer : IAsyncDisposable
{
    public static readonly TimeSpan Timeout = TimeSpan.FromSeconds(15);

    private readonly CancellationTokenSource _stop = new();
    private readonly Channel<ControlLinkStatus> _statuses = Channel.CreateUnbounded<ControlLinkStatus>();
    private readonly Channel<Message> _messages = Channel.CreateUnbounded<Message>();
    private readonly Task _pinging;
    private volatile bool _pingingPaused;

    public RunningServer(string pipeName, TimeSpan pingInterval)
    {
        PipeName = pipeName;
        Server = new ControlPipeServer(pipeName, Log);
        Server.StatusChanged += status => _statuses.Writer.TryWrite(status);
        Server.MessageReceived += message => _messages.Writer.TryWrite(message);
        // The pipe exists once this returns: RunAsync creates it before its first await.
        Run = Server.RunAsync(_stop.Token);
        _pinging = PingAsync(pingInterval, _stop.Token);
    }

    public string PipeName { get; }

    public ControlPipeServer Server { get; }

    public Task Run { get; }

    public CapturingLog Log { get; } = new();

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

    // The next message from the dock (other than a pong) that matches.
    public async Task<T> WaitForMessageAsync<T>(Func<T, bool> predicate)
        where T : Message
    {
        using var timeout = new CancellationTokenSource(Timeout);
        await foreach (var message in _messages.Reader.ReadAllAsync(timeout.Token))
        {
            if (message is T typed && predicate(typed))
            {
                return typed;
            }
        }
        throw new UnreachableException();
    }

    // Stops pinging while staying connected, like a watchdog that got stuck.
    public void PausePinging() => _pingingPaused = true;

    public async ValueTask DisposeAsync()
    {
        await _stop.CancelAsync();
        foreach (var task in new[] { Run, _pinging })
        {
            try
            {
                await task;
            }
            catch (OperationCanceledException)
            {
            }
        }
        _stop.Dispose();
    }

    private async Task PingAsync(TimeSpan interval, CancellationToken cancellationToken)
    {
        using var timer = new PeriodicTimer(interval);
        while (await timer.WaitForNextTickAsync(cancellationToken))
        {
            if (!_pingingPaused)
            {
                Server.TryPing();
            }
        }
    }
}
