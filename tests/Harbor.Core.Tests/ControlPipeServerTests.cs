using System.IO.Pipes;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Text;
using Harbor.Core.Ipc;

namespace Harbor.Core.Tests;

// Integration tests: ControlPipeServer against real pipe clients in this process.
[TestClass]
public sealed class ControlPipeServerTests
{
    private static readonly TimeSpan PingInterval = TimeSpan.FromMilliseconds(200);

    private static async Task<NamedPipeClientStream> ConnectAsync(string pipeName)
    {
        // CurrentUserOnly makes .NET verify that the pipe's owner is the current user.
        var client = new NamedPipeClientStream(
            ".", pipeName, PipeDirection.InOut, PipeOptions.Asynchronous | PipeOptions.CurrentUserOnly);
        await client.ConnectAsync((int)RunningServer.Timeout.TotalMilliseconds);
        return client;
    }

    private static Task WriteLineAsync(Stream stream, string text) =>
        stream.WriteAsync(Encoding.UTF8.GetBytes(text + "\n")).AsTask();

    // Answers every ping like the dock does, until the stream closes or the token is cancelled.
    private static async Task AnswerPingsAsync(Stream stream, CancellationToken cancellationToken)
    {
        using var reader = new StreamReader(stream, Encoding.UTF8, leaveOpen: true);
        while (await reader.ReadLineAsync(cancellationToken) is { } line)
        {
            if (MessageCodec.TryDecode(Encoding.UTF8.GetBytes(line), out var message, out _) && message is Ping ping)
            {
                await stream.WriteAsync((byte[])[.. MessageCodec.Encode(new Pong(ping.Seq)), (byte)'\n'], cancellationToken);
            }
        }
    }

    [TestMethod]
    public async Task ReportsRoundTripForAnsweredPings()
    {
        await using var server = new RunningServer(RunningServer.UniquePipeName(), PingInterval);
        await using var client = await ConnectAsync(server.PipeName);
        using var stop = new CancellationTokenSource();
        var answering = AnswerPingsAsync(client, stop.Token);

        var status = await server.WaitForAsync(s => s.LastPongSeq is not null);
        Assert.IsTrue(status.Connected);
        Assert.IsNotNull(status.RoundTrip);
        Assert.IsTrue(status.RoundTrip < RunningServer.Timeout);
        // This process is the client here.
        Assert.AreEqual((uint)Environment.ProcessId, status.ClientProcessId);
        await stop.CancelAsync();
    }

    [TestMethod]
    public async Task TrySendFailsWithoutAConnection()
    {
        await using var server = new RunningServer(RunningServer.UniquePipeName(), PingInterval);
        Assert.IsFalse(server.Server.TrySend(new Ping(1)));
        Assert.IsNull(server.Server.TryPing());
    }

    [TestMethod]
    public async Task DropsBadInputAndKeepsServing()
    {
        await using var server = new RunningServer(RunningServer.UniquePipeName(), PingInterval);
        await using var client = await ConnectAsync(server.PipeName);

        await WriteLineAsync(client, "not json");
        await WriteLineAsync(client, new string('x', LineFramer.MaxLineBytes + 10));
        await WriteLineAsync(client, Encoding.UTF8.GetString(MessageCodec.Encode(new Ping(1))));
        await WriteLineAsync(client, Encoding.UTF8.GetString(MessageCodec.Encode(new Pong(999_999))));

        using var stop = new CancellationTokenSource();
        var answering = AnswerPingsAsync(client, stop.Token);
        await server.WaitForAsync(s => s.LastPongSeq is not null);
        await stop.CancelAsync();

        var log = string.Join("\n", server.Log.Lines);
        StringAssert.Contains(log, "dropped message (MalformedJson)");
        StringAssert.Contains(log, "dropped an oversized line");
        StringAssert.Contains(log, "ignored unexpected Ping");
        StringAssert.Contains(log, "ignored stale pong 999999");
    }

    [TestMethod]
    public async Task ServesTheNextDockAfterADisconnect()
    {
        await using var server = new RunningServer(RunningServer.UniquePipeName(), PingInterval);
        await using (var first = await ConnectAsync(server.PipeName))
        {
            using var stop = new CancellationTokenSource();
            var answering = AnswerPingsAsync(first, stop.Token);
            await server.WaitForAsync(s => s.LastPongSeq is not null);
            await stop.CancelAsync();
        }
        await server.WaitForAsync(s => !s.Connected);

        await using var second = await ConnectAsync(server.PipeName);
        using var stopSecond = new CancellationTokenSource();
        var answeringSecond = AnswerPingsAsync(second, stopSecond.Token);
        var status = await server.WaitForAsync(s => s.LastPongSeq is not null);
        Assert.IsTrue(status.Connected);
        await stopSecond.CancelAsync();
    }

    [TestMethod]
    public async Task SecondServerForTheSameNameFails()
    {
        await using var first = new RunningServer(RunningServer.UniquePipeName(), PingInterval);
        var second = new ControlPipeServer(first.PipeName);

        // The first server allows a single instance, so Windows reports ERROR_PIPE_BUSY.
        var exception = await Assert.ThrowsExactlyAsync<IOException>(() => second.RunAsync(CancellationToken.None));
        Assert.AreEqual(unchecked((int)0x800700E7), exception.HResult, exception.Message);
    }

    [TestMethod]
    public async Task RefusesANameAnotherProcessCreatedFirst()
    {
        // A squatter that allows several instances: our FILE_FLAG_FIRST_PIPE_INSTANCE makes Windows refuse
        // to join its pipe with ERROR_ACCESS_DENIED instead of letting the dock talk to it.
        var pipeName = RunningServer.UniquePipeName();
        await using var squatter = new NamedPipeServerStream(pipeName, PipeDirection.InOut, maxNumberOfServerInstances: 2);
        var server = new ControlPipeServer(pipeName);

        await Assert.ThrowsExactlyAsync<UnauthorizedAccessException>(() => server.RunAsync(CancellationToken.None));
    }

    [TestMethod]
    public async Task PipeAllowsOnlyTheCurrentUserAndDeniesNetworkLogons()
    {
        await using var server = new RunningServer(RunningServer.UniquePipeName(), PingInterval);
        await using var client = await ConnectAsync(server.PipeName);

        // Read the security of the live pipe, not just the object the server builds.
        var rules = client.GetAccessControl()
            .GetAccessRules(includeExplicit: true, includeInherited: true, typeof(SecurityIdentifier))
            .Cast<PipeAccessRule>()
            .ToList();
        using var identity = WindowsIdentity.GetCurrent();
        var network = new SecurityIdentifier(WellKnownSidType.NetworkSid, null);

        Assert.HasCount(2, rules);
        Assert.IsTrue(rules.Any(r => r.AccessControlType == AccessControlType.Allow && r.IdentityReference.Equals(identity.User)));
        Assert.IsTrue(rules.Any(r => r.AccessControlType == AccessControlType.Deny && r.IdentityReference.Equals(network)));
    }
}
