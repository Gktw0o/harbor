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

    // A windowless GUI app that writes "<its path>.launched" when started (tests/dock/marker_app.cpp). Copied to
    // a path with spaces and non-ASCII characters, so launching handles both.
    public static string CopyMarkerApp(string dir)
    {
        var marker = Environment.GetEnvironmentVariable("HARBOR_MARKER_EXE");
        if (string.IsNullOrEmpty(marker))
        {
            Assert.Inconclusive("Set HARBOR_MARKER_EXE to a built harbor_test_marker.exe; scripts/build.ps1 does.");
        }
        var folder = Directory.CreateDirectory(Path.Combine(dir, "Ünïcødé ğ apps")).FullName;
        var copy = Path.Combine(folder, "marker app.exe");
        File.Copy(marker, copy);
        return copy;
    }

    // Tests that bring windows to the front move the keyboard focus away from whatever the person at this PC
    // is typing in: they run in CI, and locally only with HARBOR_FOREGROUND_TESTS=1.
    public static void RequireForegroundTests()
    {
        if (!string.Equals(Environment.GetEnvironmentVariable("CI"), "true", StringComparison.OrdinalIgnoreCase) &&
            Environment.GetEnvironmentVariable("HARBOR_FOREGROUND_TESTS") != "1")
        {
            Assert.Inconclusive("Changes the foreground: runs in CI, or set HARBOR_FOREGROUND_TESTS=1.");
        }
    }

    // Polls until check() holds; false after the timeout.
    public static async Task<bool> WaitUntilAsync(Func<Task<bool>> check, TimeSpan timeout)
    {
        var deadline = Stopwatch.StartNew();
        do
        {
            if (await check())
            {
                return true;
            }
            await Task.Delay(50);
        }
        while (deadline.Elapsed < timeout);
        return false;
    }

    // Runs tests/dock/uia_client.cpp against the island: a UI Automation client in its own process, as a screen
    // reader is. Returns its exit code and output.
    public static async Task<(int ExitCode, string Output)> RunUiaClientAsync(nint island, params string[] arguments)
    {
        using var process = StartUiaClient(island, arguments);
        return await FinishUiaClientAsync(process);
    }

    // Starts the client's "hold" on the button with that name and waits until it holds the element.
    public static async Task<Process> HoldUiaElementAsync(nint island, string name)
    {
        var process = StartUiaClient(island, "hold", name);
        Assert.AreEqual("found", await process.StandardOutput.ReadLineAsync(), $"no button named {name}");
        return process;
    }

    // Lets a holding client ask its element again. Exit code 0 and "alive\t<name>", or 5 and "gone".
    public static async Task<(int ExitCode, string Output)> ReleaseUiaElementAsync(Process client)
    {
        await client.StandardInput.WriteLineAsync();
        await client.StandardInput.FlushAsync();
        return await FinishUiaClientAsync(client);
    }

    private static Process StartUiaClient(nint island, params string[] arguments)
    {
        var client = Environment.GetEnvironmentVariable("HARBOR_UIA_CLIENT_EXE");
        if (string.IsNullOrEmpty(client))
        {
            Assert.Inconclusive("Set HARBOR_UIA_CLIENT_EXE to a built harbor_test_uia_client.exe; scripts/build.ps1 does.");
        }
        var startInfo = new ProcessStartInfo(client)
        {
            RedirectStandardInput = true,
            RedirectStandardOutput = true,
            StandardOutputEncoding = System.Text.Encoding.UTF8,
            CreateNoWindow = true,
        };
        startInfo.ArgumentList.Add(island.ToString(System.Globalization.CultureInfo.InvariantCulture));
        foreach (var argument in arguments)
        {
            startInfo.ArgumentList.Add(argument);
        }
        return Process.Start(startInfo)!;
    }

    private static async Task<(int ExitCode, string Output)> FinishUiaClientAsync(Process process)
    {
        var output = await process.StandardOutput.ReadToEndAsync();
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(30));
        await process.WaitForExitAsync(timeout.Token);
        return (process.ExitCode, output);
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

    // The test hook that makes the dock act as if its graphics device were lost (see DockWindow::kDeviceLostMessage).
    public static void LoseDevice(FakeTaskbar taskbar)
    {
        using var lose = EventWaitHandle.OpenExisting($@"Local\Harbor.Test.{taskbar.Id}.lose-device");
        lose.Set();
    }

    // The test hooks that cloak or uncloak the island, as a virtual desktop switch would (see
    // DockWindow::kTestCloakMessage). Each works once per dock.
    public static void CloakIsland(FakeTaskbar taskbar, bool cloak)
    {
        using var hook = EventWaitHandle.OpenExisting(
            $@"Local\Harbor.Test.{taskbar.Id}.{(cloak ? "cloak-island" : "uncloak-island")}");
        hook.Set();
    }

    public static async Task<string> ReadLogAsync(string dataDir)
    {
        // The dock keeps the file open for appending.
        using var reader = new StreamReader(new FileStream(
            Path.Combine(dataDir, "logs", "dock.log"), FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete));
        return await reader.ReadToEndAsync();
    }

    // Waits until the dock's log contains the text; false after the timeout.
    public static Task<bool> WaitForLogAsync(string dataDir, string text, TimeSpan timeout) =>
        WaitForLogAfterAsync(dataDir, 0, text, timeout);

    // Waits until the dock's log contains the text past the first `from` characters (LogLengthAsync before
    // an action: what the action caused); false after the timeout.
    public static async Task<bool> WaitForLogAfterAsync(string dataDir, int from, string text, TimeSpan timeout)
    {
        var deadline = Stopwatch.StartNew();
        do
        {
            if (File.Exists(Path.Combine(dataDir, "logs", "dock.log")) &&
                (await ReadLogAsync(dataDir)).IndexOf(text, from, StringComparison.Ordinal) >= 0)
            {
                return true;
            }
            await Task.Delay(50);
        }
        while (deadline.Elapsed < timeout);
        return false;
    }

    public static async Task<int> LogLengthAsync(string dataDir) => (await ReadLogAsync(dataDir)).Length;

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
