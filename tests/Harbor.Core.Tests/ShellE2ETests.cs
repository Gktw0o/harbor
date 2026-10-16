using System.Diagnostics;
using System.Runtime.InteropServices;

namespace Harbor.Core.Tests;

// End to end: the real Harbor.Shell.exe in test mode (ShellOptions), which starts the dock copied next to it in
// test mode too. scripts/build.ps1 points HARBOR_SHELL_EXE at it.
[TestClass]
public sealed partial class ShellE2ETests
{
    private static readonly TimeSpan Timeout = TimeSpan.FromSeconds(30);

    private static string ShellExe()
    {
        var exe = Environment.GetEnvironmentVariable("HARBOR_SHELL_EXE");
        if (string.IsNullOrEmpty(exe) || !File.Exists(exe))
        {
            if (string.Equals(Environment.GetEnvironmentVariable("CI"), "true", StringComparison.OrdinalIgnoreCase))
            {
                Assert.Fail($"HARBOR_SHELL_EXE must name a built Harbor.Shell.exe in CI (it is '{exe}'); scripts/build.ps1 sets it.");
            }
            Assert.Inconclusive("Set HARBOR_SHELL_EXE to a built Harbor.Shell.exe; scripts/build.ps1 does.");
        }
        return exe;
    }

    // A test Shell keeps to its own pipe, data dir and fake taskbar, starts its dock with the same three, and
    // ends when its window closes (Application.Exit): nothing of it touches the user's Harbor.
    [TestMethod]
    public async Task ATestShellStartsItsTestDockAndEndsWithItsWindow()
    {
        var exe = ShellExe();
        using var dataDir = new TempDirectory();
        using var taskbar = new FakeTaskbar(dataDir.Path);
        var pipeName = RunningServer.UniquePipeName();
        var startInfo = new ProcessStartInfo(exe) { UseShellExecute = false };
        foreach (var argument in new[] { "--pipe-name", pipeName, "--data-dir", dataDir.Path, "--test-taskbar", taskbar.Id })
        {
            startInfo.ArgumentList.Add(argument);
        }
        using var shell = Process.Start(startInfo)!;
        int? dockId = null;
        try
        {
            // The dock logs into the Shell's data dir and connects to the Shell's pipe.
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "control channel: connected to Harbor Shell", Timeout),
                await ShellLogAsync(dataDir.Path) + await TestDock.ReadLogAsync(dataDir.Path));
            var dockLog = await TestDock.ReadLogAsync(dataDir.Path);
            StringAssert.Contains(dockLog, $"control pipe {pipeName}, test taskbar");
            dockId = ProcessIdIn(dockLog);
            StringAssert.Contains(await ShellLogAsync(dataDir.Path), "test mode");

            // Its window: shown, out of sight, and the user's focus left alone.
            var window = await MainWindowOfAsync(shell);
            Assert.AreNotEqual((nint)0, window, "the test Shell has no window");
            Assert.AreNotEqual(window, IslandWindow.GetForegroundWindow(), "the test Shell took the focus");

            Assert.IsTrue(PostMessage(window, 0x0010 /* WM_CLOSE */, 0, 0));
            using var exited = new CancellationTokenSource(Timeout);
            await shell.WaitForExitAsync(exited.Token);
            Assert.AreEqual(0, shell.ExitCode);
        }
        finally
        {
            if (!shell.HasExited)
            {
                shell.Kill();
            }
            // Without its Shell the test dock would go by itself after a while; no need to wait for it.
            if (dockId is { } id)
            {
                TestDock.KillQuietly(id);
            }
        }
    }

    // A command line that would mix a test Shell with the user's real one is refused: nothing starts.
    [TestMethod]
    public async Task AHalfTestCommandLineIsRefused()
    {
        var exe = ShellExe();
        using var dataDir = new TempDirectory();
        var startInfo = new ProcessStartInfo(exe) { UseShellExecute = false };
        startInfo.ArgumentList.Add("--data-dir");
        startInfo.ArgumentList.Add(dataDir.Path);
        using var shell = Process.Start(startInfo)!;
        using var exited = new CancellationTokenSource(Timeout);
        await shell.WaitForExitAsync(exited.Token);
        Assert.AreEqual(2, shell.ExitCode);
        StringAssert.Contains(await ShellLogAsync(dataDir.Path), "invalid command line");
    }

    private static async Task<string> ShellLogAsync(string dataDir)
    {
        var path = Path.Combine(dataDir, "logs", "shell.log");
        if (!File.Exists(path))
        {
            return "(no shell.log)\n";
        }
        await using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
        using var reader = new StreamReader(stream);
        return await reader.ReadToEndAsync();
    }

    // "harbor-dock starting: <arch> build, process <id>, ...".
    private static int? ProcessIdIn(string dockLog)
    {
        const string Marker = " build, process ";
        var at = dockLog.IndexOf(Marker, StringComparison.Ordinal);
        if (at < 0)
        {
            return null;
        }
        var digits = new string(dockLog[(at + Marker.Length)..].TakeWhile(char.IsAsciiDigit).ToArray());
        return int.TryParse(digits, System.Globalization.CultureInfo.InvariantCulture, out var id) ? id : null;
    }

    private static async Task<nint> MainWindowOfAsync(Process process)
    {
        var deadline = Stopwatch.StartNew();
        while (deadline.Elapsed < Timeout)
        {
            process.Refresh();
            if (process.MainWindowHandle != 0)
            {
                return process.MainWindowHandle;
            }
            await Task.Delay(100);
        }
        return 0;
    }

    [LibraryImport("user32.dll", EntryPoint = "PostMessageW")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool PostMessage(nint window, uint message, nuint wParam, nint lParam);
}
