using System.Diagnostics;
using System.Runtime.InteropServices;

namespace Harbor.Core.Tests;

// The real harbor-dock.exe, always in test mode: stand-in taskbar windows, its own pipe and data directory.
// scripts/build.ps1 points HARBOR_DOCK_EXE at the dock built for this test run's architecture.
internal static partial class TestDock
{
    public static string ExePath
    {
        get
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
    }

    // A short ping timeout (the real one is 5 s) keeps the tests that wait for it quick.
    public static string[] Arguments(string pipeName, string dataDir, FakeTaskbar taskbar) =>
        ["--pipe-name", pipeName, "--data-dir", dataDir, "--test-taskbar", taskbar.Id, "--test-ping-timeout-ms", "1500"];

    public static Process Start(string pipeName, string dataDir, FakeTaskbar taskbar, params string[] extraArguments)
    {
        var startInfo = new ProcessStartInfo(ExePath);
        foreach (var argument in Arguments(pipeName, dataDir, taskbar).Concat(extraArguments))
        {
            startInfo.ArgumentList.Add(argument);
        }
        return Process.Start(startInfo)!;
    }

    // Presses the dock's panic hotkey the reliable way: WM_HOTKEY straight to its guard window, which is
    // titled with the pipe name.
    public static void PressPanicHotkey(string pipeName)
    {
        var guard = FindWindow("Harbor.Dock.Guard", pipeName);
        Assert.AreNotEqual((nint)0, guard, "the dock's guard window wasn't found");
        Assert.IsTrue(PostMessage(guard, WmHotkey, PanicHotkeyId, 0));
    }

    private const uint WmHotkey = 0x0312;
    private const nuint PanicHotkeyId = 1;

    [LibraryImport("user32.dll", EntryPoint = "FindWindowW", StringMarshalling = StringMarshalling.Utf16)]
    private static partial nint FindWindow(string className, string windowName);

    [LibraryImport("user32.dll", EntryPoint = "PostMessageW")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool PostMessage(nint window, uint message, nuint wParam, nint lParam);

    // The test hook that hangs only the dock's UI thread (see DockWindow::kTestHangMessage).
    public static void HangUi(FakeTaskbar taskbar)
    {
        using var hang = EventWaitHandle.OpenExisting($@"Local\Harbor.Test.{taskbar.Id}.hang-ui");
        hang.Set();
    }

    public static async Task<string> ReadLogAsync(string dataDir)
    {
        // The dock keeps the file open for appending.
        using var reader = new StreamReader(new FileStream(
            Path.Combine(dataDir, "logs", "dock.log"), FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete));
        return await reader.ReadToEndAsync();
    }

    // Waits until the dock's log contains the text; false after the timeout.
    public static async Task<bool> WaitForLogAsync(string dataDir, string text, TimeSpan timeout)
    {
        var deadline = Stopwatch.StartNew();
        do
        {
            if (File.Exists(Path.Combine(dataDir, "logs", "dock.log")) &&
                (await ReadLogAsync(dataDir)).Contains(text, StringComparison.Ordinal))
            {
                return true;
            }
            await Task.Delay(50);
        }
        while (deadline.Elapsed < timeout);
        return false;
    }

    public static void KillQuietly(int processId)
    {
        try
        {
            using var process = Process.GetProcessById(processId);
            process.Kill();
            process.WaitForExit(10_000);
        }
        catch (ArgumentException)
        {
            // Already gone.
        }
        catch (InvalidOperationException)
        {
        }
    }
}
