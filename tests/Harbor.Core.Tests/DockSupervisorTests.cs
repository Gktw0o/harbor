using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading.Channels;
using Harbor.Core.Ipc;
using Harbor.Core.Supervision;
using Harbor.Core.Taskbar;
using Microsoft.Win32.SafeHandles;

namespace Harbor.Core.Tests;

// The watchdog end to end: DockSupervisor with the real dock in test mode, against stand-in taskbar windows.
// Each test first makes things look like the dock had hidden the taskbar (windows hidden, auto-hide on, a
// restore record), then makes the dock fail in some way.
[TestClass]
public sealed partial class DockSupervisorTests
{
    private static readonly TimeSpan Timeout = TimeSpan.FromSeconds(20);

    [TestMethod]
    public async Task StartsTheDockAndPingsIt()
    {
        await using var harness = new Harness(launch: true);
        var running = await harness.WaitForDockAsync(s => s.State == DockState.Running);
        Assert.IsNotNull(running.ProcessId);
        await harness.WaitForPongAsync();
    }

    [TestMethod]
    public async Task AKilledDockGetsTheTaskbarFullyRestored()
    {
        await using var harness = new Harness(launch: true);
        var running = await harness.WaitForDockAsync(s => s.State == DockState.Running);
        await harness.WaitForPongAsync();
        harness.SimulateHidden();

        TestDock.KillQuietly((int)running.ProcessId!);

        var stopped = await harness.WaitForDockAsync(s => s.State == DockState.Stopped);
        StringAssert.Contains(stopped.Reason, "stopped unexpectedly");
        harness.AssertFullyRestored();
    }

    [TestMethod]
    public async Task AHungUiGetsTheTaskbarBackThenTheDockIsEnded()
    {
        await using var harness = new Harness(launch: true);
        await harness.WaitForDockAsync(s => s.State == DockState.Running);
        await harness.WaitForPongAsync();
        harness.SimulateHidden();

        TestDock.HangUi(harness.Taskbar);

        // First the windows, while the dock still runs; auto-hide and the record are left to after its end.
        var shown = await harness.WaitForRestoreAsync(r => r.Reason.Contains("stopped responding", StringComparison.Ordinal));
        Assert.AreEqual(2, shown.Result?.WindowsShown);
        Assert.IsTrue(harness.Snapshots.TryGetValue(shown, out var atThatMoment));
        Assert.IsTrue(atThatMoment.RecordExisted, "the watchdog touched the record while the dock was alive");
        Assert.AreEqual(1u, atThatMoment.AutoHide, "the watchdog touched auto-hide while the dock was alive");

        var stopped = await harness.WaitForDockAsync(s => s.State == DockState.Stopped);
        Assert.AreEqual(DockExitCodes.KilledByWatchdog, stopped.ExitCode);
        harness.AssertFullyRestored();
    }

    [TestMethod]
    public async Task AFrozenDockIsDetectedAndEnded()
    {
        await using var harness = new Harness(launch: true);
        var running = await harness.WaitForDockAsync(s => s.State == DockState.Running);
        await harness.WaitForPongAsync();
        harness.SimulateHidden();

        // Every thread stops, guard and pipe thread included.
        using (var dock = Process.GetProcessById((int)running.ProcessId!))
        {
            Assert.AreEqual(0, NtSuspendProcess(dock.SafeHandle));
        }

        var stopped = await harness.WaitForDockAsync(s => s.State == DockState.Stopped);
        Assert.AreEqual(DockExitCodes.KilledByWatchdog, stopped.ExitCode);
        harness.AssertFullyRestored();
    }

    [TestMethod]
    public async Task AdoptsADockThatIsAlreadyRunning()
    {
        await using var harness = new Harness(launch: false, startDockFirst: true);
        var running = await harness.WaitForDockAsync(s => s.State == DockState.Running);
        Assert.AreEqual((uint)harness.PreStartedDock!.Id, running.ProcessId);
        await harness.WaitForPongAsync();
        harness.SimulateHidden();

        TestDock.KillQuietly(harness.PreStartedDock.Id);

        await harness.WaitForDockAsync(s => s.State == DockState.Stopped);
        harness.AssertFullyRestored();
    }

    // Nothing connects: an earlier run may have left the taskbar hidden, so the Shell restores it at start.
    [TestMethod]
    public async Task RestoresAtStartupWhenNoDockConnects()
    {
        await using var harness = new Harness(launch: false, simulateHiddenFirst: true);

        var restore = await harness.WaitForRestoreAsync(r => r.Reason.Contains("earlier Harbor run", StringComparison.Ordinal));
        Assert.AreEqual(0u, restore.Result?.AutoHideRestoredTo);
        harness.AssertFullyRestored();
        var waiting = await harness.WaitForDockAsync(s => s.State == DockState.WaitingForDock);
        StringAssert.Contains(waiting.Reason, "wasn't found");
    }

    // The hide path end to end: Shell toggle, setTaskbar, the dock's guard, taskbarState back.
    [TestMethod]
    public async Task HidesTheTaskbarWhenAskedAndShowsItAgain()
    {
        await using var harness = new Harness(launch: true);
        await harness.WaitForDockAsync(s => s.State == DockState.Running);
        await harness.WaitForPongAsync();

        harness.Supervisor.SetTaskbarHidden(true);
        await harness.WaitForStateAsync(s => s.Hidden);
        harness.AssertHidden();

        harness.Supervisor.SetTaskbarHidden(false);
        await harness.WaitForStateAsync(s => !s.Hidden);
        harness.AssertFullyRestored();
    }

    [TestMethod]
    public async Task WhenTheShellGoesAwayTheDockRestoresTheTaskbar()
    {
        await using var harness = new Harness(launch: true);
        var running = await harness.WaitForDockAsync(s => s.State == DockState.Running);
        await harness.WaitForPongAsync();
        harness.Supervisor.SetTaskbarHidden(true);
        await harness.WaitForStateAsync(s => s.Hidden);
        harness.AssertHidden();

        await harness.StopSupervisorAsync();

        // Nothing restores here but the dock itself, which keeps running.
        harness.AssertFullyRestored();
        using var dock = Process.GetProcessById((int)running.ProcessId!);
        Assert.IsFalse(dock.HasExited);
    }

    [TestMethod]
    public async Task ThePanicHotkeyRestoresTheTaskbarAndTurnsHidingOff()
    {
        await using var harness = new Harness(launch: true);
        await harness.WaitForDockAsync(s => s.State == DockState.Running);
        await harness.WaitForPongAsync();
        harness.Supervisor.SetTaskbarHidden(true);
        await harness.WaitForStateAsync(s => s.Hidden);

        TestDock.PressPanicHotkey(harness.PipeName);

        await harness.WaitForStateAsync(s => s.Blocked == TaskbarBlock.PanicHotkey);
        await harness.WaitForHideRequestAsync(false);
        harness.AssertFullyRestored();

        // Turning it back on works: the Shell's "off" cleared the dock's panic block.
        harness.Supervisor.SetTaskbarHidden(true);
        await harness.WaitForStateAsync(s => s.Hidden);
        harness.AssertHidden();
    }

    [TestMethod]
    public async Task AHungDockIsToldToStopHidingThenEnded()
    {
        await using var harness = new Harness(launch: true);
        await harness.WaitForDockAsync(s => s.State == DockState.Running);
        await harness.WaitForPongAsync();
        harness.Supervisor.SetTaskbarHidden(true);
        await harness.WaitForStateAsync(s => s.Hidden);

        TestDock.HangUi(harness.Taskbar);

        await harness.WaitForHideRequestAsync(false);
        var stopped = await harness.WaitForDockAsync(s => s.State == DockState.Stopped);
        Assert.AreEqual(DockExitCodes.KilledByWatchdog, stopped.ExitCode);
        harness.AssertFullyRestored();
    }

    [TestMethod]
    [DoNotParallelize]
    public async Task AHotkeyHeldElsewhereBlocksHiding()
    {
        // Take the test docks' panic hotkey first.
        Assert.IsTrue(RegisterHotKey(0, 7, ModControl | ModAlt | ModShift | ModNoRepeat, VkF24));
        try
        {
            await using var harness = new Harness(launch: true);
            await harness.WaitForDockAsync(s => s.State == DockState.Running);
            await harness.WaitForPongAsync();

            harness.Supervisor.SetTaskbarHidden(true);
            await harness.WaitForStateAsync(s => s.Blocked == TaskbarBlock.HotkeyUnavailable);
            Assert.IsTrue(FakeTaskbar.IsVisible(harness.Taskbar.Primary));
        }
        finally
        {
            UnregisterHotKey(0, 7);
        }
    }

    private const uint ModAlt = 0x1, ModControl = 0x2, ModShift = 0x4, ModNoRepeat = 0x4000, VkF24 = 0x87;

    [LibraryImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool RegisterHotKey(nint window, int id, uint modifiers, uint key);

    [LibraryImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool UnregisterHotKey(nint window, int id);

    [LibraryImport("ntdll.dll")]
    private static partial int NtSuspendProcess(SafeProcessHandle process);

    private readonly record struct Snapshot(bool RecordExisted, uint AutoHide);

    private sealed class Harness : IAsyncDisposable
    {
        private readonly TempDirectory _dir = new();
        private readonly CancellationTokenSource _stop = new();
        private readonly Channel<DockStatus> _dockStatuses = Channel.CreateUnbounded<DockStatus>();
        private readonly Channel<TaskbarRestore> _restores = Channel.CreateUnbounded<TaskbarRestore>();
        private readonly Channel<ControlLinkStatus> _links = Channel.CreateUnbounded<ControlLinkStatus>();
        private readonly Channel<TaskbarState> _states = Channel.CreateUnbounded<TaskbarState>();
        private readonly Channel<bool> _hideRequests = Channel.CreateUnbounded<bool>();
        private readonly List<int> _docks = [];
        private readonly Task _run;

        public Harness(bool launch, bool startDockFirst = false, bool simulateHiddenFirst = false)
        {
            Taskbar = new FakeTaskbar(_dir.Path);
            var pipeName = $"harbor.test.{Guid.NewGuid():N}";
            PipeName = pipeName;
            if (simulateHiddenFirst)
            {
                SimulateHidden();
            }
            if (startDockFirst)
            {
                PreStartedDock = TestDock.Start(pipeName, _dir.Path, Taskbar);
                _docks.Add(PreStartedDock.Id);
            }

            var supervisor = new DockSupervisor(
                new DockSupervisorOptions
                {
                    DockExePath = launch ? TestDock.ExePath : null,
                    DockArguments = TestDock.Arguments(pipeName, _dir.Path, Taskbar),
                    PingInterval = TimeSpan.FromMilliseconds(100),
                    MissedPongLimit = 5,
                    // A pre-started dock needs a moment to reach the pipe.
                    AdoptWait = startDockFirst ? TimeSpan.FromSeconds(5) : TimeSpan.FromMilliseconds(200),
                    RestoreVerifyDelay = TimeSpan.Zero,
                },
                new ControlPipeServer(pipeName, Log),
                new TaskbarRestorer(Taskbar.Target, RecordPath, Log),
                Log);
            supervisor.DockStatusChanged += status =>
            {
                if (status is { State: DockState.Running, ProcessId: { } id })
                {
                    lock (_docks)
                    {
                        _docks.Add((int)id);
                    }
                }
                _dockStatuses.Writer.TryWrite(status);
            };
            supervisor.TaskbarRestored += restore =>
            {
                // What the restore left behind, at the moment it was reported.
                Snapshots[restore] = new Snapshot(File.Exists(RecordPath), Taskbar.AutoHide);
                _restores.Writer.TryWrite(restore);
            };
            supervisor.LinkStatusChanged += status => _links.Writer.TryWrite(status);
            supervisor.TaskbarStateChanged += state => _states.Writer.TryWrite(state);
            supervisor.HideRequestChanged += hidden => _hideRequests.Writer.TryWrite(hidden);
            Supervisor = supervisor;
            _run = supervisor.RunAsync(_stop.Token);
        }

        public DockSupervisor Supervisor { get; }

        public string PipeName { get; }

        public FakeTaskbar Taskbar { get; }

        public CapturingLog Log { get; } = new();

        public Process? PreStartedDock { get; }

        public System.Collections.Concurrent.ConcurrentDictionary<TaskbarRestore, Snapshot> Snapshots { get; } = new();

        private string RecordPath => RestoreRecord.PathIn(_dir.Path);

        // What the dock leaves when it has hidden the taskbar.
        public void SimulateHidden()
        {
            FakeTaskbar.Hide(Taskbar.Primary);
            FakeTaskbar.Hide(Taskbar.Secondaries[0]);
            Taskbar.AutoHide = 1;
            File.WriteAllBytes(RecordPath, RestoreRecord.Encode(new RestoreRecord(0, "2026-10-14T15:30:00Z")));
        }

        public void AssertFullyRestored()
        {
            Assert.IsTrue(FakeTaskbar.WaitUntilVisible(Taskbar.Primary, visible: true, Timeout), "primary taskbar still hidden");
            Assert.IsTrue(FakeTaskbar.WaitUntilVisible(Taskbar.Secondaries[0], visible: true, Timeout), "secondary taskbar still hidden");
            var deadline = Stopwatch.StartNew();
            while ((File.Exists(RecordPath) || Taskbar.AutoHide != 0) && deadline.Elapsed < Timeout)
            {
                Thread.Sleep(20);
            }
            Assert.AreEqual(0u, Taskbar.AutoHide, "auto-hide wasn't restored");
            Assert.IsFalse(File.Exists(RecordPath), "the restore record is still there");
        }

        public Task<DockStatus> WaitForDockAsync(Func<DockStatus, bool> predicate) => WaitAsync(_dockStatuses, predicate);

        public Task<TaskbarRestore> WaitForRestoreAsync(Func<TaskbarRestore, bool> predicate) => WaitAsync(_restores, predicate);

        public Task<ControlLinkStatus> WaitForPongAsync() => WaitAsync(_links, s => s.LastPongSeq is not null);

        public Task<TaskbarState> WaitForStateAsync(Func<TaskbarState, bool> predicate) => WaitAsync(_states, predicate);

        public Task<bool> WaitForHideRequestAsync(bool hidden) => WaitAsync(_hideRequests, h => h == hidden);

        // The windows hidden and auto-hide on, as the dock does it (auto-hide was off: the record has that).
        public void AssertHidden()
        {
            Assert.IsTrue(FakeTaskbar.WaitUntilVisible(Taskbar.Primary, visible: false, Timeout), "primary taskbar still visible");
            Assert.IsTrue(FakeTaskbar.WaitUntilVisible(Taskbar.Secondaries[0], visible: false, Timeout), "secondary taskbar still visible");
            Assert.AreEqual(1u, Taskbar.AutoHide);
            Assert.IsTrue(RestoreRecord.TryDecode(File.ReadAllBytes(RecordPath), out var record));
            Assert.AreEqual(0u, record.StateBefore);
        }

        // Harbor Shell going away (closed, crashed): its pipe closes.
        public async Task StopSupervisorAsync()
        {
            await _stop.CancelAsync();
            try
            {
                await _run;
            }
            catch (OperationCanceledException)
            {
            }
        }

        public async ValueTask DisposeAsync()
        {
            await StopSupervisorAsync();
            List<int> docks;
            lock (_docks)
            {
                docks = [.. _docks];
            }
            foreach (var dock in docks)
            {
                TestDock.KillQuietly(dock);
            }
            PreStartedDock?.Dispose();
            Taskbar.Dispose();
            _dir.Dispose();
            _stop.Dispose();
        }

        private async Task<T> WaitAsync<T>(Channel<T> channel, Func<T, bool> predicate)
        {
            using var timeout = new CancellationTokenSource(Timeout);
            try
            {
                await foreach (var item in channel.Reader.ReadAllAsync(timeout.Token))
                {
                    if (predicate(item))
                    {
                        return item;
                    }
                }
            }
            catch (OperationCanceledException)
            {
                Assert.Fail($"timed out waiting for a {typeof(T).Name}. Log:\n{string.Join("\n", Log.Lines)}");
            }
            throw new UnreachableException();
        }
    }
}
