using System.Collections.Concurrent;
using System.Threading.Channels;
using Harbor.Core.Apps;
using Harbor.Core.Ipc;

namespace Harbor.Core.Tests;

[TestClass]
public sealed class AppLookupServiceTests
{
    private static readonly TimeSpan Timeout = TimeSpan.FromSeconds(10);

    private const string Terminal = "Microsoft.WindowsTerminal_8wekyb3d8bbwe!App";

    // Answers from a dictionary; a name in Slow waits until it is released.
    private sealed class FakeResolver : IAppResolver
    {
        public ConcurrentDictionary<string, ResolvedApp> Aumids { get; } = new();
        public ConcurrentDictionary<string, ResolvedApp> Exes { get; } = new();
        public ConcurrentDictionary<string, ShortcutInfo> Shortcuts { get; } = new();
        public ConcurrentDictionary<uint, string> RelaunchNames { get; } = new();
        public ManualResetEventSlim Release { get; } = new(initialState: true);
        public int Calls;

        public ResolvedApp? ResolveAumid(string aumid)
        {
            Interlocked.Increment(ref Calls);
            Release.Wait();
            return Aumids.GetValueOrDefault(aumid);
        }

        public ResolvedApp? ResolveExe(string path)
        {
            Interlocked.Increment(ref Calls);
            Release.Wait();
            return Exes.GetValueOrDefault(path);
        }

        public string? WindowRelaunchName(uint window) => RelaunchNames.GetValueOrDefault(window);

        public ShortcutInfo? ReadShortcut(string path) => Shortcuts.GetValueOrDefault(path);
    }

    private sealed class Running : IAsyncDisposable
    {
        private readonly CancellationTokenSource _stop = new();
        private readonly Task _loop;
        private readonly Channel<AppInfo> _answers = Channel.CreateUnbounded<AppInfo>();

        // A quick answer only where a test asks for one: a slow machine mustn't make one by itself.
        public Running(FakeResolver resolver, AppLookupOptions? options = null)
        {
            Service = new AppLookupService(resolver, new CapturingLog(),
                options: options ?? new AppLookupOptions { QuickAnswer = Timeout, Timeout = Timeout * 2 });
            Service.Answered += info => _answers.Writer.TryWrite(info);
            _loop = Service.RunAsync(_stop.Token);
        }

        public AppLookupService Service { get; }

        public async Task<AppInfo> NextAsync()
        {
            using var timeout = new CancellationTokenSource(Timeout);
            return await _answers.Reader.ReadAsync(timeout.Token);
        }

        public bool HasMore => _answers.Reader.TryPeek(out _);

        public async ValueTask DisposeAsync()
        {
            await _stop.CancelAsync();
            try
            {
                await _loop;
            }
            catch (OperationCanceledException)
            {
            }
            _stop.Dispose();
        }
    }

    [TestMethod]
    public async Task AnswersWithStartsNameAndIconAndHowToStartIt()
    {
        var resolver = new FakeResolver();
        resolver.Aumids[Terminal] = new ResolvedApp("Terminal", "0123456789abcdef.bgra");
        await using var running = new Running(resolver);

        running.Service.Lookup(new AppLookup(7, Terminal, @"C:\Program Files\WindowsApps\wt.exe", 100));

        Assert.AreEqual(
            new AppInfo(7, "Terminal", "0123456789abcdef.bgra", null, new AppTarget(DockItemKind.App, Terminal)),
            await running.NextAsync());
    }

    [TestMethod]
    public async Task AnAppStartDoesntKnowGoesByItsExecutableThenItsWindow()
    {
        var resolver = new FakeResolver();
        resolver.Exes[@"C:\Tools\code.exe"] = new ResolvedApp("Visual Studio Code", null);
        resolver.RelaunchNames[300] = "Chat (Work)";
        await using var running = new Running(resolver);

        running.Service.Lookup(new AppLookup(1, "Code.Late", @"C:\Tools\code.exe", null));
        Assert.AreEqual(new AppInfo(1, "Visual Studio Code", null, null, new AppTarget(DockItemKind.Path, @"C:\Tools\code.exe")),
            await running.NextAsync());

        // Neither Start nor the file: the window's relaunch name, and nothing to start.
        running.Service.Lookup(new AppLookup(2, "Chat.Profile.Work", null, 300));
        Assert.AreEqual(new AppInfo(2, "Chat (Work)", null, null, null), await running.NextAsync());

        // Nothing at all: the file name.
        running.Service.Lookup(new AppLookup(3, null, @"C:\Gone\tool.exe", null));
        Assert.AreEqual(new AppInfo(3, "tool", null, null, null), await running.NextAsync());
    }

    [TestMethod]
    public async Task AppsAreMatchedToPinsByAppUserModelIdExecutableOrShortcut()
    {
        var resolver = new FakeResolver();
        resolver.Shortcuts[@"C:\Users\me\Desktop\Game.lnk"] = new ShortcutInfo(@"D:\Games\game.exe", "Studio.Game");
        await using var running = new Running(resolver);
        running.Service.SetPins(new DockItems(
        [
            new DockItem(DockItemKind.App, Terminal, "Terminal", null),
            new DockItem(DockItemKind.Path, @"C:\Tools\Editor.exe", "Editor", null),
            new DockItem(DockItemKind.Path, @"C:\Users\me\Desktop\Game.lnk", "Game", null),
        ]));

        running.Service.Lookup(new AppLookup(1, Terminal.ToUpperInvariant(), null, null));
        Assert.AreEqual(new AppTarget(DockItemKind.App, Terminal), (await running.NextAsync()).Pinned);
        running.Service.Lookup(new AppLookup(2, null, @"c:\tools\editor.exe", null));
        Assert.AreEqual(new AppTarget(DockItemKind.Path, @"C:\Tools\Editor.exe"), (await running.NextAsync()).Pinned);
        running.Service.Lookup(new AppLookup(3, null, @"D:\Games\game.exe", null));
        Assert.AreEqual(new AppTarget(DockItemKind.Path, @"C:\Users\me\Desktop\Game.lnk"), (await running.NextAsync()).Pinned);
        running.Service.Lookup(new AppLookup(4, "Studio.Game", null, null));
        Assert.AreEqual(new AppTarget(DockItemKind.Path, @"C:\Users\me\Desktop\Game.lnk"), (await running.NextAsync()).Pinned);
        running.Service.Lookup(new AppLookup(5, "Other.App", @"C:\Other\other.exe", null));
        Assert.IsNull((await running.NextAsync()).Pinned);
    }

    [TestMethod]
    public async Task ASlowLookupIsAnsweredAtOnceThenProperly()
    {
        var resolver = new FakeResolver();
        resolver.Aumids[Terminal] = new ResolvedApp("Terminal", "0123456789abcdef.bgra");
        resolver.Release.Reset();
        await using var running = new Running(resolver, new AppLookupOptions { QuickAnswer = TimeSpan.FromMilliseconds(50), Timeout = Timeout });

        running.Service.Lookup(new AppLookup(7, Terminal, @"C:\wt\WindowsTerminal.exe", null));
        Assert.AreEqual(new AppInfo(7, "WindowsTerminal", null, null, null), await running.NextAsync());
        resolver.Release.Set();
        Assert.AreEqual("Terminal", (await running.NextAsync()).Name);
    }

    [TestMethod]
    public async Task AHangingLookupIsGivenUp()
    {
        var resolver = new FakeResolver();
        resolver.Release.Reset();
        var log = new CapturingLog();
        var service = new AppLookupService(resolver, log,
            options: new AppLookupOptions { QuickAnswer = TimeSpan.FromMilliseconds(20), Timeout = TimeSpan.FromMilliseconds(200) });
        var answers = new ConcurrentQueue<AppInfo>();
        service.Answered += answers.Enqueue;
        using var stop = new CancellationTokenSource();
        var loop = service.RunAsync(stop.Token);

        service.Lookup(new AppLookup(9, null, @"\\server\share\app.exe", null));
        var deadline = DateTime.UtcNow + Timeout;
        while (!log.Contains("looking up app 9 took over") && DateTime.UtcNow < deadline)
        {
            await Task.Delay(20);
        }
        Assert.IsTrue(log.Contains("looking up app 9 took over"));
        // Only the quick answer.
        Assert.HasCount(1, answers);
        Assert.AreEqual("app", answers.Single().Name);
        resolver.Release.Set();
        await stop.CancelAsync();
        try
        {
            await loop;
        }
        catch (OperationCanceledException)
        {
        }
    }

    [TestMethod]
    public async Task ResultsAreKept()
    {
        var resolver = new FakeResolver();
        resolver.Aumids[Terminal] = new ResolvedApp("Terminal", null);
        await using var running = new Running(resolver);

        running.Service.Lookup(new AppLookup(1, Terminal, null, null));
        await running.NextAsync();
        running.Service.Disconnected();
        running.Service.Lookup(new AppLookup(1, Terminal, null, null));
        Assert.AreEqual("Terminal", (await running.NextAsync()).Name);
        Assert.AreEqual(1, resolver.Calls);
    }

    // After a pin change the Shell sends dockItems, then every app the dock asked about again: which pin it is may
    // have changed.
    [TestMethod]
    public async Task EveryAppIsAnsweredAgainWhenThePinsChange()
    {
        var resolver = new FakeResolver();
        resolver.Aumids[Terminal] = new ResolvedApp("Terminal", null);
        await using var running = new Running(resolver);
        running.Service.Lookup(new AppLookup(1, Terminal, null, null));
        Assert.IsNull((await running.NextAsync()).Pinned);

        running.Service.SetPins(new DockItems([new DockItem(DockItemKind.App, Terminal, "Terminal", null)]));
        Assert.AreEqual(new AppInfo(1, "Terminal", null, new AppTarget(DockItemKind.App, Terminal), new AppTarget(DockItemKind.App, Terminal)),
            await running.NextAsync());

        // Gone with the connection: nothing to answer again.
        running.Service.Disconnected();
        running.Service.SetPins(new DockItems([]));
        running.Service.Lookup(new AppLookup(2, "Other.App", null, null));
        Assert.AreEqual(2u, (await running.NextAsync()).Id);
    }

    // A pin change mustn't delete the icon of an app the dock shows (docs/adr/0010).
    [TestMethod]
    public async Task IconsItNamesAreKeptUntilTheDockGoes()
    {
        using var dataDir = new TempDirectory();
        var icons = new IconStore(dataDir.Path, new CapturingLog());
        var icon = icons.Save(new IconImage(1, 1, [9, 0, 0, 255]))!;
        var resolver = new FakeResolver();
        resolver.Aumids[Terminal] = new ResolvedApp("Terminal", icon);
        var service = new AppLookupService(resolver, new CapturingLog(), icons,
            new AppLookupOptions { QuickAnswer = Timeout, Timeout = Timeout * 2 });
        var answered = new TaskCompletionSource<AppInfo>(TaskCreationOptions.RunContinuationsAsynchronously);
        service.Answered += info => answered.TrySetResult(info);
        using var stop = new CancellationTokenSource();
        var loop = service.RunAsync(stop.Token);

        service.Lookup(new AppLookup(1, Terminal, null, null));
        Assert.AreEqual(icon, (await answered.Task.WaitAsync(Timeout)).Icon);
        icons.KeepOnly([]);
        icons.KeepOnly([]);
        Assert.IsTrue(File.Exists(Path.Combine(icons.Directory, icon)));

        service.Disconnected();
        var deadline = DateTime.UtcNow + Timeout;
        while (File.Exists(Path.Combine(icons.Directory, icon)) && DateTime.UtcNow < deadline)
        {
            icons.KeepOnly([]);
            await Task.Delay(20);
        }
        Assert.IsFalse(File.Exists(Path.Combine(icons.Directory, icon)));
        await stop.CancelAsync();
        try
        {
            await loop;
        }
        catch (OperationCanceledException)
        {
        }
    }

    [TestMethod]
    public async Task NamesAreCleanedAndBounded()
    {
        var resolver = new FakeResolver();
        resolver.Aumids["A.B"] = new ResolvedApp("Bad\u0001name " + new string('x', 200), "not-an-icon.png");
        await using var running = new Running(resolver);

        running.Service.Lookup(new AppLookup(1, "A.B", null, null));
        var info = await running.NextAsync();
        Assert.IsTrue(MessageCodec.IsValidDockItemText(info.Name, DockItems.MaxNameLength), info.Name);
        StringAssert.StartsWith(info.Name, "Bad name ");
        Assert.IsNull(info.Icon);
    }
}
