using System.Text;
using System.Threading.Channels;
using Harbor.Core.Settings;

namespace Harbor.Core.Tests;

[TestClass]
public sealed class SettingsWatcherTests
{
    private static readonly TimeSpan Timeout = TimeSpan.FromSeconds(10);
    private static readonly HarborSettings First = new([new Pin(PinKind.App, "MSEdge")]);
    private static readonly HarborSettings Second = new([new Pin(PinKind.Path, @"C:\Tools\app.exe"), new Pin(PinKind.App, "MSEdge")]);

    // A watcher running over its own data directory, with what it reports queued for the test.
    private sealed class RunningWatcher : IAsyncDisposable
    {
        private readonly CancellationTokenSource _stop = new();
        private readonly Task _run;

        public RunningWatcher(string dataDir)
        {
            Watcher = new SettingsWatcher(new SettingsFile(dataDir, Log), Log) { Settle = TimeSpan.FromMilliseconds(50) };
            Watcher.Changed += settings => Changes.Writer.TryWrite(settings);
            Watcher.ProblemChanged += problem => Problems.Writer.TryWrite(problem);
            _run = Watcher.RunAsync(_stop.Token);
        }

        public CapturingLog Log { get; } = new();

        public SettingsWatcher Watcher { get; }

        public Channel<HarborSettings> Changes { get; } = Channel.CreateUnbounded<HarborSettings>();

        public Channel<string?> Problems { get; } = Channel.CreateUnbounded<string?>();

        public static async Task<T> NextAsync<T>(Channel<T> channel)
        {
            using var timeout = new CancellationTokenSource(Timeout);
            return await channel.Reader.ReadAsync(timeout.Token);
        }

        public async ValueTask DisposeAsync()
        {
            await _stop.CancelAsync();
            try
            {
                await _run;
            }
            catch (OperationCanceledException)
            {
            }
            _stop.Dispose();
        }
    }

    [TestMethod]
    public async Task ReportsTheSettingsAndEachEdit()
    {
        using var dataDir = new TempDirectory();
        var path = HarborSettings.PathIn(dataDir.Path);
        File.WriteAllBytes(path, HarborSettings.Encode(First));
        await using var running = new RunningWatcher(dataDir.Path);

        Assert.AreEqual(First, await RunningWatcher.NextAsync(running.Changes));
        File.WriteAllBytes(path, HarborSettings.Encode(Second));
        Assert.AreEqual(Second, await RunningWatcher.NextAsync(running.Changes));
        Assert.AreEqual(Second, running.Watcher.Current);
        Assert.IsNull(running.Watcher.Problem);
    }

    // A save caught halfway, or a typo, must not empty the dock or bring back the defaults.
    [TestMethod]
    public async Task KeepsTheLastGoodSettingsWhileTheFileIsBroken()
    {
        using var dataDir = new TempDirectory();
        var path = HarborSettings.PathIn(dataDir.Path);
        File.WriteAllBytes(path, HarborSettings.Encode(First));
        await using var running = new RunningWatcher(dataDir.Path);
        Assert.AreEqual(First, await RunningWatcher.NextAsync(running.Changes));

        File.WriteAllBytes(path, Encoding.UTF8.GetBytes("""{"v":1,"pinned":[{"app":"""));
        StringAssert.Contains(await RunningWatcher.NextAsync(running.Problems), "isn't valid JSON");
        Assert.AreEqual(First, running.Watcher.Current);
        Assert.IsFalse(running.Changes.Reader.TryRead(out var unexpected), $"reported {unexpected}");

        File.WriteAllBytes(path, HarborSettings.Encode(Second));
        Assert.AreEqual(Second, await RunningWatcher.NextAsync(running.Changes));
        Assert.IsNull(await RunningWatcher.NextAsync(running.Problems));
    }

    [TestMethod]
    public async Task StartsWithTheDefaultsWhenTheFileIsBroken()
    {
        using var dataDir = new TempDirectory();
        var path = HarborSettings.PathIn(dataDir.Path);
        var broken = Encoding.UTF8.GetBytes("""{"v":1,"pinned":[{"path":"relative.exe"}]}""");
        File.WriteAllBytes(path, broken);
        await using var running = new RunningWatcher(dataDir.Path);

        Assert.AreEqual(HarborSettings.Defaults, await RunningWatcher.NextAsync(running.Changes));
        StringAssert.Contains(running.Watcher.Problem, "isn't an absolute path");
        CollectionAssert.AreEqual(broken, File.ReadAllBytes(path));
    }

    [TestMethod]
    public async Task KeepsTheSettingsWhenTheFileIsDeleted()
    {
        using var dataDir = new TempDirectory();
        var path = HarborSettings.PathIn(dataDir.Path);
        File.WriteAllBytes(path, HarborSettings.Encode(First));
        await using var running = new RunningWatcher(dataDir.Path);
        Assert.AreEqual(First, await RunningWatcher.NextAsync(running.Changes));

        File.Delete(path);
        StringAssert.Contains(await RunningWatcher.NextAsync(running.Problems), "deleted");
        Assert.AreEqual(First, running.Watcher.Current);
        Assert.IsFalse(File.Exists(path), "the defaults were written while Harbor Shell runs");
    }
}
