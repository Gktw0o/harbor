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
            // Nor can Windows hand it the focus (the user's Alt+Tab once landed on it during a local run).
            Assert.AreEqual(IslandWindow.ExNoActivate, IslandWindow.ExStyle(window) & IslandWindow.ExNoActivate, "the test Shell's window can be activated");

            // Closing the island changes nothing (it closes to its notch); closing the main window ends the Shell.
            var island = IslandWindow.FindShellIsland(shell.Id);
            if (island != 0)
            {
                Assert.IsTrue(PostMessage(island, 0x0010 /* WM_CLOSE */, 0, 0));
                await Task.Delay(500);
                Assert.IsFalse(shell.HasExited, "closing the island ended the Shell");
                Assert.AreEqual(island, IslandWindow.FindShellIsland(shell.Id), "the island went");
            }
            Assert.IsTrue(PostMessage(window, 0x0010 /* WM_CLOSE */, 0, 0));
            var exit = shell.WaitForExitAsync();
            var ended = await Task.WhenAny(exit, Task.Delay(Timeout)) == exit;
            Assert.IsTrue(ended, "closing its main window didn't end the Shell: " + await ShellLogAsync(dataDir.Path));
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

    // The island (docs/adr/0013): a glass notch flush with the top edge of the dock's monitor, a tool window that
    // stays on top and never takes the focus; it tells the dock it is ready, with its hotkey, and hides while a
    // full-screen app is in front there.
    [TestMethod]
    public async Task TheIslandSitsAtTheTopOfTheDocksMonitorAndHidesForFullScreen()
    {
        var exe = ShellExe();
        using var dataDir = new TempDirectory();
        using var taskbar = new FakeTaskbar(dataDir.Path);
        using var shell = StartShell(exe, dataDir.Path, taskbar);
        int? dockId = null;
        try
        {
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "island: ready", Timeout),
                await ShellLogAsync(dataDir.Path) + await TestDock.ReadLogAsync(dataDir.Path));
            dockId = ProcessIdIn(await TestDock.ReadLogAsync(dataDir.Path));
            var island = IslandWindow.FindShellIsland(shell.Id);
            Assert.AreNotEqual((nint)0, island, "no island window");
            Assert.IsTrue(await TestDock.WaitUntilAsync(() => Task.FromResult(IslandWindow.IsWindowVisible(island)), Timeout), "the island isn't shown");
            var style = IslandWindow.ExStyle(island);
            foreach (var flag in new[] { IslandWindow.ExToolWindow, IslandWindow.ExTopmost, IslandWindow.ExNoActivate, IslandWindow.ExNoRedirectionBitmap })
            {
                Assert.AreEqual(flag, style & flag, $"extended style 0x{style:X} lacks 0x{flag:X}");
            }
            Assert.AreNotEqual(island, IslandWindow.GetForegroundWindow(), "the island took the focus");

            // Flush with the top of the primary monitor (the test dock's), centred, the notch's size.
            var (monitor, _) = IslandWindow.PrimaryMonitor();
            var bounds = IslandWindow.Bounds(island);
            Assert.AreEqual(monitor.Top, bounds.Top, $"island {bounds} on {monitor}");
            Assert.IsLessThanOrEqualTo(1, Math.Abs((bounds.Left - monitor.Left) - (monitor.Right - bounds.Right)), $"island {bounds} on {monitor}");
            Assert.IsTrue(bounds.Width is > 100 and < 400 && bounds.Height is > 10 and < 80, $"not a notch: {bounds}");
            StringAssert.Contains(await TestDock.ReadLogAsync(dataDir.Path), "island: ready");

            TestDock.SetFullScreen(taskbar, fullScreen: true);
            Assert.IsTrue(await TestDock.WaitUntilAsync(() => Task.FromResult(!IslandWindow.IsWindowVisible(island)), Timeout),
                "the island stayed over a full-screen app");
            TestDock.SetFullScreen(taskbar, fullScreen: false);
            Assert.IsTrue(await TestDock.WaitUntilAsync(() => Task.FromResult(IslandWindow.IsWindowVisible(island)), Timeout),
                "the island didn't come back after full screen");
        }
        finally
        {
            await StopShellAsync(shell, dockId);
        }
    }

    // For real (CI only: it takes the focus and may open Windows' Start): the dock's Start button opens the island in
    // front with its focus, Esc closes it, and with Harbor Shell gone the Start button opens Windows' Start instead.
    [TestMethod]
    [DoNotParallelize]
    public async Task TheStartButtonOpensTheIslandAndEscClosesIt()
    {
        TestDock.RequireForegroundTests();
        var exe = ShellExe();
        using var dataDir = new TempDirectory();
        using var taskbar = new FakeTaskbar(dataDir.Path);
        using var shell = StartShell(exe, dataDir.Path, taskbar, foreground: true);
        int? dockId = null;
        try
        {
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "island: ready", Timeout),
                await ShellLogAsync(dataDir.Path) + await TestDock.ReadLogAsync(dataDir.Path));
            dockId = ProcessIdIn(await TestDock.ReadLogAsync(dataDir.Path));
            var dock = IslandWindow.Find(dockId!.Value);
            var island = IslandWindow.FindShellIsland(shell.Id);
            var collapsed = IslandWindow.Bounds(island);

            Assert.AreEqual(0, (await TestDock.RunUiaClientAsync(dock, "invoke", "Start")).ExitCode);
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "ms after the Start button", Timeout),
                await ShellLogAsync(dataDir.Path) + await TestDock.ReadLogAsync(dataDir.Path));
            Assert.IsTrue(await TestDock.WaitUntilAsync(() => Task.FromResult(IslandWindow.GetForegroundWindow() == island), Timeout),
                "the island didn't get the focus: " + await ShellLogAsync(dataDir.Path));
            Assert.IsTrue(await TestDock.WaitUntilAsync(() => Task.FromResult(IslandWindow.Bounds(island).Width > collapsed.Width * 2), Timeout),
                $"not open: {IslandWindow.Bounds(island)}");

            IslandWindow.PressEscape();
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "island: closed", Timeout),
                await ShellLogAsync(dataDir.Path) + await TestDock.ReadLogAsync(dataDir.Path));
            Assert.IsFalse((await TestDock.ReadLogAsync(dataDir.Path)).Contains("Windows Start instead", StringComparison.Ordinal));

            // Harbor Shell gone: the dock forgets the island, and its Start button opens Windows' Start.
            shell.Kill();
            await shell.WaitForExitAsync();
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "island: not ready", Timeout), await TestDock.ReadLogAsync(dataDir.Path));
            var from = await TestDock.LogLengthAsync(dataDir.Path);
            Assert.AreEqual(0, (await TestDock.RunUiaClientAsync(dock, "invoke", "Start")).ExitCode);
            Assert.IsTrue(await TestDock.WaitForLogAfterAsync(dataDir.Path, from, "click: Start (UI Automation): Windows Start", Timeout),
                await TestDock.ReadLogAsync(dataDir.Path));
        }
        finally
        {
            await TestDock.CloseWindowsStartAsync();
            await StopShellAsync(shell, dockId);
        }
    }

    // For real (CI only: it takes the focus): the Windows key's path minus the key itself. The Start button opens
    // the island, the user types, Enter starts the app through the dock, and the island closes. The test Shell
    // searches only its test list, which names the marker app.
    [TestMethod]
    [DoNotParallelize]
    public async Task TheIslandFindsAnAppAndStartsItThroughTheDock()
    {
        TestDock.RequireForegroundTests();
        var exe = ShellExe();
        using var dataDir = new TempDirectory();
        using var taskbar = new FakeTaskbar(dataDir.Path);
        var app = TestDock.CopyMarkerApp(dataDir.Path);
        await File.WriteAllTextAsync(Path.Combine(dataDir.Path, "test-apps.json"),
            System.Text.Json.JsonSerializer.Serialize(new[] { new { name = "Harbor Marker", id = app }, new { name = "Something Else", id = @"C:\nothing.exe" } }));
        using var shell = StartShell(exe, dataDir.Path, taskbar, foreground: true);
        int? dockId = null;
        try
        {
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "island: ready", Timeout),
                await ShellLogAsync(dataDir.Path) + await TestDock.ReadLogAsync(dataDir.Path));
            dockId = ProcessIdIn(await TestDock.ReadLogAsync(dataDir.Path));
            var dock = IslandWindow.Find(dockId!.Value);
            var island = IslandWindow.FindShellIsland(shell.Id);
            Assert.IsTrue(await TestDock.WaitUntilAsync(async () => (await ShellLogAsync(dataDir.Path)).Contains("apps: 2 in the island's list", StringComparison.Ordinal), Timeout),
                await ShellLogAsync(dataDir.Path));

            Assert.AreEqual(0, (await TestDock.RunUiaClientAsync(dock, "invoke", "Start")).ExitCode);
            Assert.IsTrue(await TestDock.WaitUntilAsync(() => Task.FromResult(IslandWindow.GetForegroundWindow() == island), Timeout),
                "the island didn't get the focus: " + await ShellLogAsync(dataDir.Path));
            await Task.Delay(300);
            IslandWindow.Type("marker");
            await Task.Delay(300);
            IslandWindow.PressEnter();

            var marker = app + ".launched";
            Assert.IsTrue(await TestDock.WaitUntilAsync(() => Task.FromResult(File.Exists(marker)), Timeout),
                await ShellLogAsync(dataDir.Path) + await TestDock.ReadLogAsync(dataDir.Path));
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "launch: from the island started via", Timeout), await TestDock.ReadLogAsync(dataDir.Path));
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "island: closed", Timeout), await TestDock.ReadLogAsync(dataDir.Path));
            Assert.IsFalse((await ShellLogAsync(dataDir.Path)).Contains("Harbor Marker", StringComparison.OrdinalIgnoreCase), "the Shell's log names an app");
        }
        finally
        {
            await StopShellAsync(shell, dockId);
        }
    }

    // For real (CI only: it takes the focus): the open island shows the machine's stats, read while it is open,
    // and offers the power actions. None is pressed: a test never risks locking or shutting down its machine.
    [TestMethod]
    [DoNotParallelize]
    public async Task TheOpenIslandShowsStatsAndOffersPower()
    {
        TestDock.RequireForegroundTests();
        var exe = ShellExe();
        using var dataDir = new TempDirectory();
        using var taskbar = new FakeTaskbar(dataDir.Path);
        using var shell = StartShell(exe, dataDir.Path, taskbar, foreground: true);
        int? dockId = null;
        try
        {
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "island: ready", Timeout),
                await ShellLogAsync(dataDir.Path) + await TestDock.ReadLogAsync(dataDir.Path));
            dockId = ProcessIdIn(await TestDock.ReadLogAsync(dataDir.Path));
            var dock = IslandWindow.Find(dockId!.Value);
            var island = IslandWindow.FindShellIsland(shell.Id);
            Assert.AreEqual(0, (await TestDock.RunUiaClientAsync(dock, "invoke", "Start")).ExitCode);
            Assert.IsTrue(await TestDock.WaitUntilAsync(() => Task.FromResult(IslandWindow.GetForegroundWindow() == island), Timeout),
                "the island didn't get the focus: " + await ShellLogAsync(dataDir.Path));

            const int Text = 50020;
            const int Button = 50000;
            var tree = "";
            Assert.IsTrue(await TestDock.WaitUntilAsync(async () =>
            {
                tree = (await TestDock.RunUiaClientAsync(island, "tree")).Output;
                return tree.Split(['\r', '\n']).Any(line => line.StartsWith($"{Text}\tCPU ", StringComparison.Ordinal) && line.Contains('%', StringComparison.Ordinal));
            }, Timeout), "no CPU reading in the open island:\n" + tree);
            StringAssert.Contains(tree, $"{Text}\tMemory ");
            foreach (var action in new[] { "Lock", "Sign out", "Sleep", "Restart", "Shut down" })
            {
                StringAssert.Contains(tree, $"{Button}\t{action}", tree);
            }
            // A test Shell never sees the machine's media: nothing playing, its buttons there but off.
            StringAssert.Contains(tree, $"{Text}\tNothing playing", tree);
            foreach (var media in new[] { "Previous", "Play", "Next" })
            {
                StringAssert.Contains(tree, $"{Button}\t{media}", tree);
            }

            IslandWindow.PressEscape();
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "island: closed", Timeout), await TestDock.ReadLogAsync(dataDir.Path));
            Assert.IsFalse((await ShellLogAsync(dataDir.Path)).Contains("power: ", StringComparison.Ordinal), "a power action ran");
        }
        finally
        {
            await StopShellAsync(shell, dockId);
        }
    }

    [TestMethod]
    [DoNotParallelize]
    public async Task ATestShellsControlsAreMadeUpAndTouchNothing()
    {
        TestDock.RequireForegroundTests();
        var exe = ShellExe();
        using var dataDir = new TempDirectory();
        using var taskbar = new FakeTaskbar(dataDir.Path);
        var themeBefore = ReadTheme();
        using var shell = StartShell(exe, dataDir.Path, taskbar, foreground: true);
        int? dockId = null;
        try
        {
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "island: ready", Timeout),
                await ShellLogAsync(dataDir.Path) + await TestDock.ReadLogAsync(dataDir.Path));
            dockId = ProcessIdIn(await TestDock.ReadLogAsync(dataDir.Path));
            var dock = IslandWindow.Find(dockId!.Value);
            var island = IslandWindow.FindShellIsland(shell.Id);
            Assert.AreEqual(0, (await TestDock.RunUiaClientAsync(dock, "invoke", "Start")).ExitCode);
            Assert.IsTrue(await TestDock.WaitUntilAsync(() => Task.FromResult(IslandWindow.GetForegroundWindow() == island), Timeout),
                "the island didn't get the focus: " + await ShellLogAsync(dataDir.Path));

            const int Button = 50000;
            const int Slider = 50015;
            var tree = "";
            Assert.IsTrue(await TestDock.WaitUntilAsync(async () =>
            {
                tree = (await TestDock.RunUiaClientAsync(island, "tree")).Output;
                return tree.Contains($"{Slider}\tVolume", StringComparison.Ordinal);
            }, Timeout), "no volume in the open island:\n" + tree);
            foreach (var control in new[] { "Mute", "Wi-Fi", "Bluetooth", "Dark mode", "Focus", "Night light", "Airplane mode", "Power mode" })
            {
                StringAssert.Contains(tree, $"{Button}\t{control}", tree);
            }
            // The made-up controls have no brightness, like a desktop PC: the island hides it.
            Assert.IsFalse(tree.Contains("\tBrightness", StringComparison.Ordinal), tree);

            var dark = await TestDock.RunUiaClientAsync(island, "toggle", "Dark mode");
            Assert.AreEqual(0, dark.ExitCode, dark.Output);
            StringAssert.Contains(dark.Output, "toggle\tOn");
            Assert.IsTrue(await WaitForShellLogAsync(dataDir.Path, "controls: test dark"), await ShellLogAsync(dataDir.Path));
            var wifi = await TestDock.RunUiaClientAsync(island, "toggle", "Wi-Fi");
            Assert.AreEqual(0, wifi.ExitCode, wifi.Output);
            Assert.IsTrue(await WaitForShellLogAsync(dataDir.Path, "controls: test WiFi off"), await ShellLogAsync(dataDir.Path));

            // A Settings link closes the island first; a test Shell only says which page it would open.
            Assert.AreEqual(0, (await TestDock.RunUiaClientAsync(island, "invoke", "Night light")).ExitCode);
            Assert.IsTrue(await WaitForShellLogAsync(dataDir.Path, "controls: test settings NightLight"), await ShellLogAsync(dataDir.Path));
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "island: closed", Timeout), await TestDock.ReadLogAsync(dataDir.Path));
            Assert.AreEqual(themeBefore, ReadTheme(), "a test Shell changed the theme");
        }
        finally
        {
            await StopShellAsync(shell, dockId);
        }
    }

    private static (object? Apps, object? Windows) ReadTheme()
    {
        using var key = Microsoft.Win32.Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize");
        return (key?.GetValue("AppsUseLightTheme"), key?.GetValue("SystemUsesLightTheme"));
    }

    private static Task<bool> WaitForShellLogAsync(string dataDir, string text) =>
        TestDock.WaitUntilAsync(async () => (await ShellLogAsync(dataDir)).Contains(text, StringComparison.Ordinal), Timeout);

    private static Process StartShell(string exe, string dataDir, FakeTaskbar taskbar, bool foreground = false)
    {
        var startInfo = new ProcessStartInfo(exe) { UseShellExecute = false };
        foreach (var argument in new[] { "--pipe-name", RunningServer.UniquePipeName(), "--data-dir", dataDir, "--test-taskbar", taskbar.Id })
        {
            startInfo.ArgumentList.Add(argument);
        }
        if (foreground)
        {
            startInfo.ArgumentList.Add("--test-foreground");
        }
        return Process.Start(startInfo)!;
    }

    private static async Task StopShellAsync(Process shell, int? dockId)
    {
        if (!shell.HasExited)
        {
            shell.Kill();
            await shell.WaitForExitAsync();
        }
        if (dockId is { } id)
        {
            TestDock.KillQuietly(id);
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

    // By its title: Process.MainWindowHandle may be the island, another visible window of the Shell's.
    private static async Task<nint> MainWindowOfAsync(Process process)
    {
        var deadline = Stopwatch.StartNew();
        while (deadline.Elapsed < Timeout)
        {
            nint window = 0;
            while ((window = FindWindowEx(0, window, null, "Harbor Shell (test)")) != 0)
            {
                _ = GetWindowThreadProcessId(window, out var owner);
                if (owner == process.Id)
                {
                    return window;
                }
            }
            await Task.Delay(100);
        }
        return 0;
    }

    [LibraryImport("user32.dll", EntryPoint = "FindWindowExW", StringMarshalling = StringMarshalling.Utf16)]
    private static partial nint FindWindowEx(nint parent, nint childAfter, string? className, string? windowName);

    [LibraryImport("user32.dll")]
    private static partial uint GetWindowThreadProcessId(nint window, out int processId);

    [LibraryImport("user32.dll", EntryPoint = "PostMessageW")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool PostMessage(nint window, uint message, nuint wParam, nint lParam);
}
