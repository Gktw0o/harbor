using System.Diagnostics;

namespace Harbor.Core.Tests;

// End to end: the real harbor-dock.exe against the real ControlPipeServer. scripts/build.ps1 points
// HARBOR_DOCK_EXE at the dock built for the same architecture as this test run.
[TestClass]
public sealed class DockE2ETests
{
    [TestMethod]
    public async Task DockAnswersPingsOverTheControlPipe()
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

        await using var server = new RunningServer($"harbor.e2e.{Guid.NewGuid():N}", TimeSpan.FromMilliseconds(100));
        using var dataDir = new TempDirectory();
        using var dock = Process.Start(new ProcessStartInfo(dockExe)
        {
            ArgumentList = { "--pipe-name", server.PipeName, "--data-dir", dataDir.Path },
        })!;
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
            using var logFile = new StreamReader(new FileStream(
                Path.Combine(dataDir.Path, "logs", "dock.log"), FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete));
            var log = await logFile.ReadToEndAsync();
            StringAssert.Contains(log, "harbor-dock starting");
            StringAssert.Contains(log, "control channel: connected to Harbor Shell");
        }
        finally
        {
            if (!dock.HasExited)
            {
                dock.Kill();
            }
            await dock.WaitForExitAsync();
        }
    }
}
