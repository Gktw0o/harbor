using System.Diagnostics;
using System.IO.Pipes;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Threading.Channels;
using Harbor.Core.Interop;
using Harbor.Core.Logging;

namespace Harbor.Core.Ipc;

/// <summary>State of the control link, as seen by Harbor.Shell.</summary>
/// <param name="Connected">Whether a dock is connected.</param>
/// <param name="LastPongSeq">Seq of the most recent pong that answered the latest ping.</param>
/// <param name="RoundTrip">Time between sending that ping and receiving its pong.</param>
/// <param name="ClientProcessId">The connected dock's process id, if Windows reported it.</param>
public sealed record ControlLinkStatus(bool Connected, uint? LastPongSeq = null, TimeSpan? RoundTrip = null, uint? ClientProcessId = null);

/// <summary>
/// Server end of the control channel. Serves one dock at a time and carries messages; it doesn't decide
/// when to ping (the watchdog does, so a ping proves the watchdog is alive). Bad input is logged and
/// dropped; a dock that disconnects is replaced by the next one that connects.
/// </summary>
public sealed class ControlPipeServer(string pipeName, ILog? log = null)
{
    // A dock that stops reading for this many messages is hung; the watchdog deals with it.
    private const int OutboxCapacity = 64;

    private volatile Connection? _connection;

    /// <summary>Raised on a background thread whenever the link status changes.</summary>
    public event Action<ControlLinkStatus>? StatusChanged;

    /// <summary>Messages from the dock other than pongs, raised on a background thread.</summary>
    public event Action<Message>? MessageReceived;

    public bool IsConnected => _connection is not null;

    /// <summary>
    /// Serves connections until cancelled, then throws <see cref="OperationCanceledException"/>. If the pipe
    /// name is taken, throws <see cref="IOException"/> (another Harbor.Shell serves it) or
    /// <see cref="UnauthorizedAccessException"/> (another process created it first). The pipe exists by the
    /// time this first returns to the caller.
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
            var connection = new Connection(
                NativeMethods.GetNamedPipeClientProcessId(pipe.SafePipeHandle, out var clientProcessId) ? clientProcessId : null);
            _connection = connection;
            StatusChanged?.Invoke(new ControlLinkStatus(Connected: true, ClientProcessId: connection.ProcessId));
            try
            {
                await ServeAsync(pipe, connection, cancellationToken).ConfigureAwait(false);
            }
            finally
            {
                _connection = null;
            }

            cancellationToken.ThrowIfCancellationRequested();
            StatusChanged?.Invoke(new ControlLinkStatus(Connected: false));
        }
    }

    /// <summary>Queues a message for the connected dock. False if none is connected or it stopped reading.</summary>
    public bool TrySend(Message message)
    {
        ArgumentNullException.ThrowIfNull(message);
        var connection = _connection;
        return connection is not null && connection.Outbox.Writer.TryWrite([.. MessageCodec.Encode(message), (byte)'\n']);
    }

    /// <summary>
    /// Sends the next ping; only a pong for it counts from now on. Returns its seq, or null if no dock is
    /// connected or it stopped reading.
    /// </summary>
    public uint? TryPing()
    {
        var connection = _connection;
        if (connection is null)
        {
            return null;
        }
        var seq = connection.Outstanding.Next(Stopwatch.GetTimestamp());
        return TrySend(new Ping(seq)) ? seq : null;
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

    private async Task ServeAsync(NamedPipeServerStream pipe, Connection connection, CancellationToken cancellationToken)
    {
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        var reading = ReadLoopAsync(pipe, connection, linked.Token);
        var writing = WriteLoopAsync(pipe, connection, linked.Token);

        // Either loop ending means the connection is over: the dock went away, or a write failed.
        await Task.WhenAny(reading, writing).ConfigureAwait(false);
        connection.Outbox.Writer.TryComplete();
        await linked.CancelAsync().ConfigureAwait(false);
        await IgnoreDisconnectAsync(reading).ConfigureAwait(false);
        await IgnoreDisconnectAsync(writing).ConfigureAwait(false);
    }

    private static async Task WriteLoopAsync(NamedPipeServerStream pipe, Connection connection, CancellationToken cancellationToken)
    {
        // The only writer, so lines never interleave.
        await foreach (var line in connection.Outbox.Reader.ReadAllAsync(cancellationToken).ConfigureAwait(false))
        {
            await pipe.WriteAsync(line, cancellationToken).ConfigureAwait(false);
        }
    }

    private async Task ReadLoopAsync(NamedPipeServerStream pipe, Connection connection, CancellationToken cancellationToken)
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
                HandleLine(line, connection);
            }
            if (framer.DroppedLineCount != droppedBefore)
            {
                log?.Warn("control channel: dropped an oversized line");
            }
        }
    }

    private void HandleLine(byte[] line, Connection connection)
    {
        if (!MessageCodec.TryDecode(line, out var message, out var error))
        {
            log?.Warn($"control channel: dropped message ({error})");
            return;
        }
        switch (message)
        {
            case Pong pong when connection.Outstanding.TryMatch(pong.Seq, out var sentAt):
                StatusChanged?.Invoke(new ControlLinkStatus(
                    Connected: true, pong.Seq, Stopwatch.GetElapsedTime(sentAt), connection.ProcessId));
                break;
            case Pong pong:
                log?.Warn($"control channel: ignored stale pong {pong.Seq}");
                break;
            case Ping:
                log?.Warn("control channel: ignored unexpected Ping");
                break;
            default:
                MessageReceived?.Invoke(message);
                break;
        }
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

    private sealed class Connection(uint? processId)
    {
        public uint? ProcessId { get; } = processId;

        public Channel<byte[]> Outbox { get; } = Channel.CreateBounded<byte[]>(
            new BoundedChannelOptions(OutboxCapacity) { SingleReader = true, FullMode = BoundedChannelFullMode.Wait });

        public OutstandingPing Outstanding { get; } = new();
    }

    // The latest ping sent; written by whoever pings, read by the read loop.
    private sealed class OutstandingPing
    {
        private readonly Lock _lock = new();
        private bool _sent;
        private uint _seq;
        private long _sentAt;

        public uint Next(long sentAt)
        {
            lock (_lock)
            {
                _sent = true;
                _seq = unchecked(_seq + 1);
                _sentAt = sentAt;
                return _seq;
            }
        }

        public bool TryMatch(uint seq, out long sentAt)
        {
            lock (_lock)
            {
                sentAt = _sentAt;
                return _sent && seq == _seq;
            }
        }
    }
}
