using System.Diagnostics;
using System.Runtime.InteropServices;
using Harbor.Core.Ipc;

namespace Harbor.Core.Tests;

// End to end, M4 step 7: what the dock and Harbor Shell's island tell each other (docs/adr/0013, 0015, 0016),
// with the real harbor-dock.exe in test mode.
[TestClass]
public sealed partial class DockIslandE2ETests
{
    private static readonly TimeSpan Timeout = TimeSpan.FromSeconds(15);

    private static async Task StopAsync(Process dock)
    {
        if (!dock.HasExited)
        {
            dock.Kill();
        }
        await dock.WaitForExitAsync();
    }

    // On connect the dock says where it is, and again when a full-screen app opens there or the panic hotkey takes
    // it away: the island follows the dock's monitor and hides with it.
    [TestMethod]
    public async Task TheDockTellsHarborShellWhereItIsAndWhatHappensThere()
    {
        await using var server = new RunningServer($"harbor.e2e.{Guid.NewGuid():N}", TimeSpan.FromMilliseconds(100));
        using var dataDir = new TempDirectory();
        using var taskbar = new FakeTaskbar(dataDir.Path);
        using var dock = TestDock.Start(server.PipeName, dataDir.Path, taskbar);
        try
        {
            var shown = await server.WaitForMessageAsync<DockStateMessage>(s => s.Visible);
            Assert.IsFalse(shown.FullScreen);
            Assert.IsTrue(shown.Dpi is >= DockStateMessage.MinDpi and <= DockStateMessage.MaxDpi, shown.ToString());
            Assert.IsTrue(shown.Monitor.Width > 0 && shown.Monitor.Height > 0, shown.ToString());
            Assert.IsTrue(shown.Work.Left >= shown.Monitor.Left && shown.Work.Top >= shown.Monitor.Top &&
                          shown.Work.Right <= shown.Monitor.Right && shown.Work.Bottom <= shown.Monitor.Bottom, shown.ToString());

            TestDock.SetFullScreen(taskbar, fullScreen: true);
            var fullScreen = await server.WaitForMessageAsync<DockStateMessage>(s => s.FullScreen);
            Assert.AreEqual(shown.Monitor, fullScreen.Monitor);
            Assert.IsTrue(fullScreen.Visible, "tucked is still shown");

            TestDock.SetFullScreen(taskbar, fullScreen: false);
            await server.WaitForMessageAsync<DockStateMessage>(s => !s.FullScreen);
            // The panic hotkey takes the dock away only while it hides the taskbar.
            Assert.IsTrue(server.Server.TrySend(new SetTaskbar(Hidden: true)));
            await server.WaitForMessageAsync<TaskbarState>(s => s.Hidden);
            TestDock.PressPanicHotkey(server.PipeName);
            await server.WaitForMessageAsync<DockStateMessage>(s => !s.Visible);
        }
        finally
        {
            await StopAsync(dock);
        }
    }

    // The Start button: with the island not ready, Windows' Start at once; ready, the island's hotkey, then
    // Windows' Start if the island doesn't open within 500 ms. Locally a test dock presses and opens neither (no
    // --test-foreground), but says what it would have done; in CI it does both, and Esc closes Start again.
    [TestMethod]
    [DoNotParallelize]
    public async Task TheStartButtonOpensTheIslandOrFallsBackToWindowsStart()
    {
        await using var server = new RunningServer($"harbor.e2e.{Guid.NewGuid():N}", TimeSpan.FromMilliseconds(100));
        using var dataDir = new TempDirectory();
        using var taskbar = new FakeTaskbar(dataDir.Path);
        using var dock = TestDock.Start(server.PipeName, dataDir.Path, taskbar);
        try
        {
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "island: shown", Timeout), await TestDock.ReadLogAsync(dataDir.Path));
            var island = IslandWindow.Find(dock.Id);

            var from = await TestDock.LogLengthAsync(dataDir.Path);
            Assert.AreEqual(0, (await TestDock.RunUiaClientAsync(island, "invoke", "Start")).ExitCode);
            Assert.IsTrue(await TestDock.WaitForLogAfterAsync(dataDir.Path, from, "click: Start (UI Automation): Windows Start", Timeout),
                await TestDock.ReadLogAsync(dataDir.Path));
            Assert.IsFalse((await TestDock.ReadLogAsync(dataDir.Path))[from..].Contains("hotkey", StringComparison.Ordinal),
                "pressed the hotkey of an island that isn't ready");
            await CloseStartAsync();

            Assert.IsTrue(server.Server.TrySend(new IslandState(Ready: true, Open: false, Hotkey: 0x85)));
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "island: ready", Timeout));
            from = await TestDock.LogLengthAsync(dataDir.Path);
            var watch = Stopwatch.StartNew();
            Assert.AreEqual(0, (await TestDock.RunUiaClientAsync(island, "invoke", "Start")).ExitCode);
            Assert.IsTrue(await TestDock.WaitForLogAfterAsync(dataDir.Path, from, "click: Start (UI Automation): the island's hotkey", Timeout),
                await TestDock.ReadLogAsync(dataDir.Path));
            Assert.IsTrue(await TestDock.WaitForLogAfterAsync(dataDir.Path, from, "Windows Start instead", Timeout),
                await TestDock.ReadLogAsync(dataDir.Path));
            Assert.IsGreaterThanOrEqualTo(450L, watch.ElapsedMilliseconds, "fell back to Windows' Start before the island had its moment");
        }
        finally
        {
            await CloseStartAsync();
            await StopAsync(dock);
        }
    }

    private static Task CloseStartAsync() => TestDock.CloseWindowsStartAsync();

    // While the island is open the Start button says so (a dot, and its status for screen readers), and another
    // press is for closing it: no fallback then.
    [TestMethod]
    public async Task TheStartButtonShowsTheIslandIsOpen()
    {
        await using var server = new RunningServer($"harbor.e2e.{Guid.NewGuid():N}", TimeSpan.FromMilliseconds(100));
        using var dataDir = new TempDirectory();
        using var taskbar = new FakeTaskbar(dataDir.Path);
        using var dock = TestDock.Start(server.PipeName, dataDir.Path, taskbar);
        try
        {
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "island: shown", Timeout), await TestDock.ReadLogAsync(dataDir.Path));
            var island = IslandWindow.Find(dock.Id);
            async Task<string> StatusAsync() => (await TestDock.RunUiaClientAsync(island, "status", "Start")).Output.TrimEnd('\r', '\n');
            Assert.AreEqual("status\t", await StatusAsync());

            Assert.IsTrue(server.Server.TrySend(new IslandState(Ready: true, Open: true, Hotkey: 0x85)));
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "island: open", Timeout), await TestDock.ReadLogAsync(dataDir.Path));
            Assert.AreEqual("status\tisland open", await StatusAsync());

            // In CI the key is pressed for real; nothing has it registered here, so it goes nowhere.
            var from = await TestDock.LogLengthAsync(dataDir.Path);
            Assert.AreEqual(0, (await TestDock.RunUiaClientAsync(island, "invoke", "Start")).ExitCode);
            Assert.IsTrue(await TestDock.WaitForLogAfterAsync(dataDir.Path, from, "the island's hotkey", Timeout));
            await Task.Delay(800);
            Assert.IsFalse((await TestDock.ReadLogAsync(dataDir.Path))[from..].Contains("Windows Start", StringComparison.Ordinal),
                "fell back to Windows' Start while the island was open");

            // Closed again: no dot.
            Assert.IsTrue(server.Server.TrySend(new IslandState(Ready: true, Open: false, Hotkey: 0x85)));
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "island: closed", Timeout));
            Assert.AreEqual("status\t", await StatusAsync());
        }
        finally
        {
            await StopAsync(dock);
        }
    }

    // An app chosen in the island starts through the dock (Explorer's route), not as Harbor Shell's child; the log
    // says where it came from, never which app.
    [TestMethod]
    public async Task ALaunchFromTheIslandStartsTheApp()
    {
        await using var server = new RunningServer($"harbor.e2e.{Guid.NewGuid():N}", TimeSpan.FromMilliseconds(100));
        using var dataDir = new TempDirectory();
        using var taskbar = new FakeTaskbar(dataDir.Path);
        var app = TestDock.CopyMarkerApp(dataDir.Path);
        using var dock = TestDock.Start(server.PipeName, dataDir.Path, taskbar);
        try
        {
            await server.WaitForAsync(s => s.Connected);
            Assert.IsTrue(server.Server.TrySend(new Launch(new AppTarget(DockItemKind.Path, app))));

            var marker = app + ".launched";
            var deadline = Stopwatch.StartNew();
            while (!File.Exists(marker) && deadline.Elapsed < TimeSpan.FromSeconds(20))
            {
                await Task.Delay(50);
            }
            Assert.IsTrue(File.Exists(marker), await TestDock.ReadLogAsync(dataDir.Path));
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "launch: from the island started via", Timeout),
                await TestDock.ReadLogAsync(dataDir.Path));
            Assert.IsFalse((await TestDock.ReadLogAsync(dataDir.Path)).Contains("marker app", StringComparison.OrdinalIgnoreCase),
                "the log names the user's app");
        }
        finally
        {
            await StopAsync(dock);
        }
    }

    // For real (CI only: the hotkey and Windows' Start are global): the Start button presses the island's hotkey,
    // which its owner receives; once it says the island is open, Windows' Start doesn't open.
    [TestMethod]
    [DoNotParallelize]
    public async Task TheStartButtonOpensTheIslandThroughItsHotkey()
    {
        TestDock.RequireForegroundTests();
        await using var server = new RunningServer($"harbor.e2e.{Guid.NewGuid():N}", TimeSpan.FromMilliseconds(100));
        using var dataDir = new TempDirectory();
        using var taskbar = new FakeTaskbar(dataDir.Path);
        using var hotkey = new ThreadHotkey();
        using var dock = TestDock.Start(server.PipeName, dataDir.Path, taskbar);
        try
        {
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "island: shown", Timeout), await TestDock.ReadLogAsync(dataDir.Path));
            var island = IslandWindow.Find(dock.Id);
            Assert.IsTrue(server.Server.TrySend(new IslandState(Ready: true, Open: false, Hotkey: hotkey.Key)));
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "island: ready", Timeout));

            Assert.AreEqual(0, (await TestDock.RunUiaClientAsync(island, "invoke", "Start")).ExitCode);
            var arrived = await Task.WhenAny(hotkey.Pressed, Task.Delay(Timeout)) == hotkey.Pressed;
            Assert.IsTrue(arrived, "the island's hotkey never came: " + await TestDock.ReadLogAsync(dataDir.Path));
            Assert.IsTrue(server.Server.TrySend(new IslandState(Ready: true, Open: true, Hotkey: hotkey.Key)));
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "ms after the Start button", Timeout),
                await TestDock.ReadLogAsync(dataDir.Path));
            await Task.Delay(800);
            Assert.IsFalse((await TestDock.ReadLogAsync(dataDir.Path)).Contains("Windows Start instead", StringComparison.Ordinal),
                await TestDock.ReadLogAsync(dataDir.Path));
        }
        finally
        {
            await StopAsync(dock);
        }
    }

    // One of F13-F24, registered as a hotkey on a thread of its own (as Harbor Shell does for the island), which
    // completes Pressed when it comes.
    private sealed partial class ThreadHotkey : IDisposable
    {
        private const uint WmHotkey = 0x0312;
        private const uint WmQuit = 0x0012;
        private readonly Thread _thread;
        private readonly TaskCompletionSource _pressed = new(TaskCreationOptions.RunContinuationsAsynchronously);
        private readonly TaskCompletionSource<int> _registered = new(TaskCreationOptions.RunContinuationsAsynchronously);
        private uint _threadId;

        public ThreadHotkey()
        {
            _thread = new Thread(Run) { IsBackground = true };
            _thread.Start();
            Key = _registered.Task.GetAwaiter().GetResult();
            Assert.AreNotEqual(0, Key, "no key from F13 to F24 could be registered");
        }

        public int Key { get; }
        public Task Pressed => _pressed.Task;

        private void Run()
        {
            _threadId = GetCurrentThreadId();
            var key = 0;
            for (var candidate = 0x7C; candidate <= 0x87 && key == 0; candidate++)
            {
                key = RegisterHotKey(0, 1, 0x4000 /* MOD_NOREPEAT */, (uint)candidate) ? candidate : 0;
            }
            _registered.SetResult(key);
            while (GetMessage(out var message, 0, 0, 0) > 0)
            {
                if (message.Message == WmHotkey)
                {
                    _pressed.TrySetResult();
                }
            }
            UnregisterHotKey(0, 1);
        }

        public void Dispose()
        {
            PostThreadMessage(_threadId, WmQuit, 0, 0);
            _thread.Join(TimeSpan.FromSeconds(5));
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct Msg
        {
            public nint Window;
            public uint Message;
            public nuint WParam;
            public nint LParam;
            public uint Time;
            public int X;
            public int Y;
        }

        [LibraryImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static partial bool RegisterHotKey(nint window, int id, uint modifiers, uint key);

        [LibraryImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static partial bool UnregisterHotKey(nint window, int id);

        [LibraryImport("user32.dll", EntryPoint = "GetMessageW")]
        private static partial int GetMessage(out Msg message, nint window, uint min, uint max);

        [LibraryImport("user32.dll", EntryPoint = "PostThreadMessageW")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static partial bool PostThreadMessage(uint threadId, uint message, nuint wParam, nint lParam);

        [LibraryImport("kernel32.dll")]
        private static partial uint GetCurrentThreadId();
    }
}
