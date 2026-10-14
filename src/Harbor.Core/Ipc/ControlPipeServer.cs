using System.Diagnostics;
using System.IO.Pipes;
using System.Security.AccessControl;
using System.Security.Principal;

namespace Harbor.Core.Ipc;

/// <summary>State of the control link, as seen by Harbor.Shell.</summary>
/// <param name="Connected">Whether a dock is connected.</param>
/// <param name="LastPongSeq">Seq of the most recent pong that answered the latest ping.</param>
/// <param name="RoundTrip">Time between sending that ping and receiving its pong.</param>
public sealed record ControlLinkStatus(bool Connected, uint? LastPongSeq = null, TimeSpan? RoundTrip = null);

/// <summary>
/// Server end of the control channel. Serves one dock at a time: sends a ping every interval and reports
/// each pong that answers the latest ping. Bad input is logged and dropped; a dock that disconnects is
/// replaced by the next one that connects.
/// </summary>
public sealed class ControlPipeServer(string pipeName, TimeSpan pingInterval, Action<string>? log = null)
{
    /// <summary>Raised on a background thread whenever the link status changes.</summary>
    public event Action<ControlLinkStatus>? StatusChanged;

    /// <summary>
    /// Serves connections until cancelled, then throws <see cref="OperationCanceledException"/>. If the pipe
    /// name is taken, throws <see cref="IOException"/> (another Harbor.Shell serves it) or
    /// <see cref="UnauthorizedAccessException"/> (another process created it first).
    /// </summary>
    public async Task RunAsync(CancellationToken cancellationToken)
    {
        while (true)
        {
            cancellationToken.ThrowIfCancellationRequested();
            // One instance: the pipe is created with FILE_FLAG_FIRST_PIPE_INSTANCE, so a squatter makes this throw.
            await using var pipe = NamedPipeServerStreamAcl.Create(
                pipeName, PipeDirection.InOut, maxNumberOfServerInstances: 1, PipeTransmissionMode.Byte,
                PipeOptions.Asynchronous, inBufferSize: 0, outBufferSize: 0, CreatePipeSecurity());

            StatusChanged?.Invoke(new ControlLinkStatus(Connected: false));
            await pipe.WaitForConnectionAsync(cancellationToken).ConfigureAwait(false);
            StatusChanged?.Invoke(new ControlLinkStatus(Connected: true));
            await ServeAsync(pipe, cancellationToken).ConfigureAwait(false);
        }
    }

    /// <summary>
    /// Only the current user may connect, and never over the network. System.IO.Pipes can't pass
    /// PIPE_REJECT_REMOTE_CLIENTS, so the NETWORK SID (present on every network logon) is denied instead.
    /// </summary>
    internal static PipeSecurity CreatePipeSecurity()
    {
        using var identity = WindowsIdentity.GetCurrent();
        var security = new PipeSecurity();
        security.AddAccessRule(new PipeAccessRule(identity.User!, PipeAccessRights.FullControl, AccessControlType.Allow));
        security.AddAccessRule(new PipeAccessRule(
            new SecurityIdentifier(WellKnownSidType.NetworkSid, null), PipeAccessRights.FullControl, AccessControlType.Deny));
        // Same owner as PipeOptions.CurrentUserOnly uses, so .NET clients that check it accept this pipe.
        security.SetOwner(identity.Owner!);
        return security;
    }

    private async Task ServeAsync(NamedPipeServerStream pipe, CancellationToken cancellationToken)
    {
        using var connection = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        var outstanding = new OutstandingPing();
        var reading = ReadLoopAsync(pipe, outstanding, connection.Token);
        var pinging = PingLoopAsync(pipe, outstanding, connection.Token);

        // Either loop ending means the connection is over: the dock went away, or a write failed.
        await Task.WhenAny(reading, pinging).ConfigureAwait(false);
        await connection.CancelAsync().ConfigureAwait(false);
        await IgnoreDisconnectAsync(reading).ConfigureAwait(false);
        await IgnoreDisconnectAsync(pinging).ConfigureAwait(false);

        cancellationToken.ThrowIfCancellationRequested();
        StatusChanged?.Invoke(new ControlLinkStatus(Connected: false));
    }

    private async Task PingLoopAsync(NamedPipeServerStream pipe, OutstandingPing outstanding, CancellationToken cancellationToken)
    {
        using var timer = new PeriodicTimer(pingInterval);
        uint seq = 0;
        do
        {
            seq = unchecked(seq + 1);
            outstanding.Set(seq, Stopwatch.GetTimestamp());
            byte[] line = [.. MessageCodec.Encode(new Ping(seq)), (byte)'\n'];
            await pipe.WriteAsync(line, cancellationToken).ConfigureAwait(false);
        }
        while (await timer.WaitForNextTickAsync(cancellationToken).ConfigureAwait(false));
    }

    private async Task ReadLoopAsync(NamedPipeServerStream pipe, OutstandingPing outstanding, CancellationToken cancellationToken)
    {
        var framer = new LineFramer();
        var buffer = new byte[4096];
        while (true)
        {
            var read = await pipe.ReadAsync(buffer, cancellationToken).ConfigureAwait(false);
            if (read == 0)
            {
                return;
            }

            var droppedBefore = framer.DroppedLineCount;
            foreach (var line in framer.Push(buffer.AsSpan(0, read)))
            {
                HandleLine(line, outstanding);
            }
            if (framer.DroppedLineCount != droppedBefore)
            {
                log?.Invoke("control channel: dropped an oversized line");
            }
        }
    }

    private void HandleLine(byte[] line, OutstandingPing outstanding)
    {
        if (!MessageCodec.TryDecode(line, out var message, out var error))
        {
            log?.Invoke($"control channel: dropped message ({error})");
            return;
        }
        if (message is not Pong pong)
        {
            log?.Invoke($"control channel: ignored unexpected {message.GetType().Name}");
            return;
        }
        if (!outstanding.TryMatch(pong.Seq, out var sentAt))
        {
            log?.Invoke($"control channel: ignored stale pong {pong.Seq}");
            return;
        }
        StatusChanged?.Invoke(new ControlLinkStatus(Connected: true, pong.Seq, Stopwatch.GetElapsedTime(sentAt)));
    }

    private static async Task IgnoreDisconnectAsync(Task task)
    {
        try
        {
            await task.ConfigureAwait(false);
        }
        catch (Exception exception) when (exception is OperationCanceledException or IOException or ObjectDisposedException)
        {
        }
    }

    // The latest ping sent; written by the ping loop, read by the read loop.
    private sealed class OutstandingPing
    {
        private readonly Lock _lock = new();
        private uint _seq;
        private long _sentAt;

        public void Set(uint seq, long sentAt)
        {
            lock (_lock)
            {
                (_seq, _sentAt) = (seq, sentAt);
            }
        }

        public bool TryMatch(uint seq, out long sentAt)
        {
            lock (_lock)
            {
                sentAt = _sentAt;
                return seq == _seq;
            }
        }
    }
}
