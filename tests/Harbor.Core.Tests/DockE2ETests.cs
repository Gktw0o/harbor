using System.Diagnostics;
using Harbor.Core.Ipc;
using Harbor.Core.Supervision;
using Harbor.Core.Taskbar;

namespace Harbor.Core.Tests;

// End to end: the real harbor-dock.exe in test mode (see TestDock).
[TestClass]
public sealed class DockE2ETests
{
    private static async Task StopAsync(Process dock)
    {
        if (!dock.HasExited)
        {
            dock.Kill();
        }
        await dock.WaitForExitAsync();
    }

    [TestMethod]
    public async Task DockAnswersPingsOverTheControlPipe()
    {
        await using var server = new RunningServer($"harbor.e2e.{Guid.NewGuid():N}", TimeSpan.FromMilliseconds(100));
        using var dataDir = new TempDirectory();
        using var taskbar = new FakeTaskbar(dataDir.Path);
        using var dock = TestDock.Start(server.PipeName, dataDir.Path, taskbar);
        try
        {
            // Three distinct pongs, each answering the latest ping: the dock's UI thread keeps responding.
            var answered = new HashSet<uint>();
            while (answered.Count < 3)
            {
                var status = await server.WaitForAsync(s => s.LastPongSeq is not null);
                answered.Add(status.LastPongSeq!.Value);
            }
            Assert.IsFalse(dock.HasExited, $"the dock exited with code {(dock.HasExited ? dock.ExitCode : 0)}");

            // --data-dir took effect: the dock logs there, not to %LOCALAPPDATA%.
            var log = await TestDock.ReadLogAsync(dataDir.Path);
            StringAssert.Contains(log, "harbor-dock starting");
            StringAssert.Contains(log, "control channel: connected to Harbor Shell");
        }
        finally
        {
            await StopAsync(dock);
        }
    }

    [TestMethod]
    public async Task DockReceivesItsAppsOverTheControlPipe()
    {
        await using var server = new RunningServer($"harbor.e2e.{Guid.NewGuid():N}", TimeSpan.FromMilliseconds(100));
        using var dataDir = new TempDirectory();
        using var taskbar = new FakeTaskbar(dataDir.Path);
        using var dock = TestDock.Start(server.PipeName, dataDir.Path, taskbar);
        try
        {
            await server.WaitForAsync(s => s.Connected);
            Assert.IsTrue(server.Server.TrySend(new DockItems(
            [
                new DockItem(DockItemKind.App, "Microsoft.Windows.Explorer", "File Explorer", null),
                new DockItem(DockItemKind.Path, @"C:\Ünïcødé\app.exe", "Ünïcødé \U0001D538", "0123456789abcdef.bgra"),
            ])));
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "dock items: 2 received", TimeSpan.FromSeconds(10)),
                await TestDock.ReadLogAsync(dataDir.Path));
        }
        finally
        {
            await StopAsync(dock);
        }
    }

    // A test dock whose test went away without ending it exits by itself, normally (taskbar restored).
    [TestMethod]
    public async Task TestDockExitsWhenNoShellConnects()
    {
        using var dataDir = new TempDirectory();
        using var taskbar = new FakeTaskbar(dataDir.Path);
        using var dock = TestDock.Start($"harbor.e2e.{Guid.NewGuid():N}", dataDir.Path, taskbar, "--test-orphan-timeout-ms", "500");
        try
        {
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(15));
            await dock.WaitForExitAsync(timeout.Token);
            Assert.AreEqual(0, dock.ExitCode);
            StringAssert.Contains(await TestDock.ReadLogAsync(dataDir.Path), "test dock: no connection to Harbor Shell for 500 ms; exiting");
        }
        finally
        {
            await StopAsync(dock);
        }
    }

    // An earlier dock died with the taskbar hidden and auto-hide changed: the next dock puts both back
    // before it does anything else.
    [TestMethod]
    public async Task DockRestoresTheTaskbarWhenItStarts()
    {
        using var dataDir = new TempDirectory();
        using var taskbar = new FakeTaskbar(dataDir.Path);
        FakeTaskbar.Hide(taskbar.Primary);
        FakeTaskbar.Hide(taskbar.Secondaries[0]);
        taskbar.AutoHide = 1;
        var recordPath = RestoreRecord.PathIn(dataDir.Path);
        File.WriteAllBytes(recordPath, RestoreRecord.Encode(new RestoreRecord(0, "2026-10-14T15:30:00Z")));

        // No Shell: recovery doesn't wait for one.
        using var dock = TestDock.Start($"harbor.e2e.{Guid.NewGuid():N}", dataDir.Path, taskbar);
        try
        {
            Assert.IsTrue(FakeTaskbar.WaitUntilVisible(taskbar.Primary, visible: true, TimeSpan.FromSeconds(10)));
            Assert.IsTrue(FakeTaskbar.WaitUntilVisible(taskbar.Secondaries[0], visible: true, TimeSpan.FromSeconds(10)));
            // The record goes only after the restored state is verified (about 1 s later).
            var deadline = Stopwatch.StartNew();
            while (File.Exists(recordPath) && deadline.Elapsed < TimeSpan.FromSeconds(10))
            {
                await Task.Delay(50);
            }
            Assert.IsFalse(File.Exists(recordPath), "the dock didn't delete the restore record");
            Assert.AreEqual(0u, taskbar.AutoHide);
            StringAssert.Contains(await TestDock.ReadLogAsync(dataDir.Path), "auto-hide restored to off (recorded 2026-10-14T15:30:00Z)");
        }
        finally
        {
            await StopAsync(dock);
        }
    }

    // Two docks would fight over the taskbar: the second one exits at once with its own exit code.
    [TestMethod]
    public async Task ASecondDockExitsBecauseOneIsRunning()
    {
        using var dataDir = new TempDirectory();
        using var taskbar = new FakeTaskbar(dataDir.Path);
        var pipeName = $"harbor.e2e.{Guid.NewGuid():N}";
        using var first = TestDock.Start(pipeName, dataDir.Path, taskbar);
        try
        {
            // Wait until the first one holds its single-instance mutex (it logs right after).
            var deadline = Stopwatch.StartNew();
            while (!(File.Exists(Path.Combine(dataDir.Path, "logs", "dock.log")) &&
                     (await TestDock.ReadLogAsync(dataDir.Path)).Contains("process job:", StringComparison.Ordinal)))
            {
                Assert.IsLessThan(TimeSpan.FromSeconds(10), deadline.Elapsed, "the first dock didn't start");
                await Task.Delay(50);
            }

            using var second = TestDock.Start(pipeName, dataDir.Path, taskbar);
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10));
            await second.WaitForExitAsync(timeout.Token);
            Assert.AreEqual(DockExitCodes.AlreadyRunning, second.ExitCode);
            Assert.IsFalse(first.HasExited);
        }
        finally
        {
            await StopAsync(first);
        }
    }
}
