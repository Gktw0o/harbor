using System.Diagnostics;
using Harbor.Core.Taskbar;

namespace Harbor.Core.Tests;

// End to end: the real harbor-dock.exe, always in test mode (fake taskbar windows, its own pipe and data
// directory). scripts/build.ps1 points HARBOR_DOCK_EXE at the dock built for this test run's architecture.
[TestClass]
public sealed class DockE2ETests
{
    private static string DockExe()
    {
        var dockExe = Environment.GetEnvironmentVariable("HARBOR_DOCK_EXE");
        if (string.IsNullOrEmpty(dockExe))
        {
            if (string.Equals(Environment.GetEnvironmentVariable("CI"), "true", StringComparison.OrdinalIgnoreCase))
            {
                Assert.Fail("HARBOR_DOCK_EXE must be set in CI; scripts/build.ps1 sets it.");
            }
            Assert.Inconclusive("Set HARBOR_DOCK_EXE to a built harbor-dock.exe to run this test; scripts/build.ps1 does.");
        }
        Assert.IsTrue(File.Exists(dockExe), $"HARBOR_DOCK_EXE points at a missing file: {dockExe}");
        return dockExe;
    }

    private static Process StartDock(string pipeName, string dataDir, FakeTaskbar taskbar) =>
        Process.Start(new ProcessStartInfo(DockExe())
        {
            ArgumentList = { "--pipe-name", pipeName, "--data-dir", dataDir, "--test-taskbar", taskbar.Id },
        })!;

    private static async Task StopAsync(Process dock)
    {
        if (!dock.HasExited)
        {
            dock.Kill();
        }
        await dock.WaitForExitAsync();
    }

    private static async Task<string> ReadDockLogAsync(string dataDir)
    {
        // The dock keeps the file open for appending.
        using var reader = new StreamReader(new FileStream(
            Path.Combine(dataDir, "logs", "dock.log"), FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete));
        return await reader.ReadToEndAsync();
    }

    [TestMethod]
    public async Task DockAnswersPingsOverTheControlPipe()
    {
        await using var server = new RunningServer($"harbor.e2e.{Guid.NewGuid():N}", TimeSpan.FromMilliseconds(100));
        using var dataDir = new TempDirectory();
        using var taskbar = new FakeTaskbar(dataDir.Path);
        using var dock = StartDock(server.PipeName, dataDir.Path, taskbar);
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
            var log = await ReadDockLogAsync(dataDir.Path);
            StringAssert.Contains(log, "harbor-dock starting");
            StringAssert.Contains(log, "control channel: connected to Harbor Shell");
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
        using var dock = StartDock($"harbor.e2e.{Guid.NewGuid():N}", dataDir.Path, taskbar);
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
            StringAssert.Contains(await ReadDockLogAsync(dataDir.Path), "auto-hide restored to off (recorded 2026-10-14T15:30:00Z)");
        }
        finally
        {
            await StopAsync(dock);
        }
    }
}
