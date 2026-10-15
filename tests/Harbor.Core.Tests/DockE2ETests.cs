using System.Diagnostics;
using Harbor.Core.Apps;
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

    // The island: a small topmost tool window that never takes focus, at the bottom of the primary monitor's
    // work area (until the AppBar reserves its own strip), as wide as its items.
    [TestMethod]
    public async Task DockShowsItsIslandAtTheBottomOfThePrimaryMonitor()
    {
        await using var server = new RunningServer($"harbor.e2e.{Guid.NewGuid():N}", TimeSpan.FromMilliseconds(100));
        using var dataDir = new TempDirectory();
        using var taskbar = new FakeTaskbar(dataDir.Path);
        using var dock = TestDock.Start(server.PipeName, dataDir.Path, taskbar);
        try
        {
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "island: shown", TimeSpan.FromSeconds(15)),
                await TestDock.ReadLogAsync(dataDir.Path));
            var window = IslandWindow.Find(dock.Id);
            Assert.AreNotEqual((nint)0, window, "no island window");
            Assert.IsTrue(IslandWindow.IsWindowVisible(window));
            var style = IslandWindow.ExStyle(window);
            foreach (var flag in new[] { IslandWindow.ExToolWindow, IslandWindow.ExTopmost, IslandWindow.ExNoActivate, IslandWindow.ExNoRedirectionBitmap })
            {
                Assert.AreEqual(flag, style & flag, $"extended style 0x{style:X} lacks 0x{flag:X}");
            }
            Assert.AreNotEqual(window, IslandWindow.GetForegroundWindow(), "the island took the focus");

            var (monitor, work) = IslandWindow.PrimaryMonitor();
            var empty = IslandWindow.Bounds(window);
            Assert.IsTrue(empty.Left >= monitor.Left && empty.Right <= monitor.Right && empty.Bottom < work.Bottom &&
                          empty.Bottom > work.Bottom - empty.Height, $"island {empty} in work area {work}");
            // Centred.
            Assert.IsLessThanOrEqualTo(1, Math.Abs((empty.Left - monitor.Left) - (monitor.Right - empty.Right)));

            await server.WaitForAsync(s => s.Connected);
            Assert.IsTrue(server.Server.TrySend(new DockItems(
            [
                new DockItem(DockItemKind.App, "A", "A", null),
                new DockItem(DockItemKind.App, "B", "B", null),
                new DockItem(DockItemKind.App, "C", "C", null),
            ])));
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "island: 3 items", TimeSpan.FromSeconds(10)));
            var three = IslandWindow.Bounds(window);
            Assert.IsGreaterThan(empty.Width, three.Width);
            Assert.AreEqual(empty.Height, three.Height);
            Assert.AreEqual(empty.Bottom, three.Bottom);

            // Its strip came from the AppBar worker (a test AppBar, which reserves nothing).
            StringAssert.Contains(await TestDock.ReadLogAsync(dataDir.Path), $"appbar: strip {monitor.Left},");
            Assert.AreEqual(work.Bottom, IslandWindow.PrimaryMonitor().Work.Bottom, "a test dock changed the work area");
        }
        finally
        {
            await StopAsync(dock);
        }
    }

    // The Shell's icon files reach the island; an item whose file is missing gets a placeholder.
    [TestMethod]
    public async Task DockDrawsTheIconFilesTheShellLeaves()
    {
        await using var server = new RunningServer($"harbor.e2e.{Guid.NewGuid():N}", TimeSpan.FromMilliseconds(100));
        using var dataDir = new TempDirectory();
        using var taskbar = new FakeTaskbar(dataDir.Path);
        var icon = new IconStore(dataDir.Path, new CapturingLog()).Save(new IconImage(2, 2, [0, 0, 255, 255, 0, 128, 0, 128, 0, 0, 0, 0, 255, 255, 255, 255]));
        using var dock = TestDock.Start(server.PipeName, dataDir.Path, taskbar);
        try
        {
            await server.WaitForAsync(s => s.Connected);
            Assert.IsTrue(server.Server.TrySend(new DockItems(
            [
                new DockItem(DockItemKind.App, "A", "A", icon),
                new DockItem(DockItemKind.App, "B", "B", "0123456789abcdef.bgra"),
            ])));
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "island: 2 items (1 icons)", TimeSpan.FromSeconds(10)),
                await TestDock.ReadLogAsync(dataDir.Path));
        }
        finally
        {
            await StopAsync(dock);
        }
    }

    // A click on an item starts its app: through Explorer where it can, else with ShellExecuteEx (as on the ARM
    // CI runner). Either way the route is logged.
    [TestMethod]
    public async Task AClickStartsThePinnedApp()
    {
        await using var server = new RunningServer($"harbor.e2e.{Guid.NewGuid():N}", TimeSpan.FromMilliseconds(100));
        using var dataDir = new TempDirectory();
        using var taskbar = new FakeTaskbar(dataDir.Path);
        var app = TestDock.CopyMarkerApp(dataDir.Path);
        using var dock = TestDock.Start(server.PipeName, dataDir.Path, taskbar);
        try
        {
            await server.WaitForAsync(s => s.Connected);
            Assert.IsTrue(server.Server.TrySend(new DockItems([new DockItem(DockItemKind.Path, app, "Marker", null)])));
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "island: 1 items", TimeSpan.FromSeconds(10)));
            var island = IslandWindow.Find(dock.Id);

            // The only item's middle: the island is as tall as one item plus its padding.
            var size = IslandWindow.Bounds(island).Height;
            IslandWindow.Click(island, size / 2, size / 2);

            var marker = app + ".launched";
            var deadline = Stopwatch.StartNew();
            while (!File.Exists(marker) && deadline.Elapsed < TimeSpan.FromSeconds(20))
            {
                await Task.Delay(50);
            }
            Assert.IsTrue(File.Exists(marker), await TestDock.ReadLogAsync(dataDir.Path));
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "launch: item 0 started via", TimeSpan.FromSeconds(5)));
            Assert.IsFalse((await TestDock.ReadLogAsync(dataDir.Path)).Contains("marker app", StringComparison.OrdinalIgnoreCase),
                "the log names the user's app");
        }
        finally
        {
            await StopAsync(dock);
            // The marker app may hold its file for a moment.
            await Task.Delay(200);
        }
    }

    // A screen reader (any UI Automation client) sees a tool bar of buttons named after the apps, and starting one
    // through its Invoke pattern works like a click.
    [TestMethod]
    public async Task ScreenReadersSeeTheAppsAndCanStartOne()
    {
        const int ToolBar = 50021;
        const int Button = 50000;
        await using var server = new RunningServer($"harbor.e2e.{Guid.NewGuid():N}", TimeSpan.FromMilliseconds(100));
        using var dataDir = new TempDirectory();
        using var taskbar = new FakeTaskbar(dataDir.Path);
        var app = TestDock.CopyMarkerApp(dataDir.Path);
        using var dock = TestDock.Start(server.PipeName, dataDir.Path, taskbar);
        try
        {
            await server.WaitForAsync(s => s.Connected);
            Assert.IsTrue(server.Server.TrySend(new DockItems(
            [
                new DockItem(DockItemKind.Path, @"C:\Harbor test\missing.exe", "Ünïcødé \U0001D538", null),
                new DockItem(DockItemKind.Path, app, "Marker", null),
            ])));
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "island: 2 items", TimeSpan.FromSeconds(10)));
            var island = IslandWindow.Find(dock.Id);

            var (listed, tree) = await TestDock.RunUiaClientAsync(island, "list");
            Assert.AreEqual(0, listed, tree);
            CollectionAssert.AreEqual(
                new[] { $"{ToolBar}\tHarbor Dock", $"{Button}\tÜnïcødé \U0001D538", $"{Button}\tMarker" },
                tree.Split(['\r', '\n'], StringSplitOptions.RemoveEmptyEntries), tree);

            var (invoked, _) = await TestDock.RunUiaClientAsync(island, "invoke", "Marker");
            Assert.AreEqual(0, invoked);
            var marker = app + ".launched";
            var deadline = Stopwatch.StartNew();
            while (!File.Exists(marker) && deadline.Elapsed < TimeSpan.FromSeconds(20))
            {
                await Task.Delay(50);
            }
            Assert.IsTrue(File.Exists(marker), await TestDock.ReadLogAsync(dataDir.Path));
            StringAssert.Contains(await TestDock.ReadLogAsync(dataDir.Path), "launch: item 1 (UI Automation)");

            Assert.AreEqual(3, (await TestDock.RunUiaClientAsync(island, "invoke", "Nothing")).ExitCode);
        }
        finally
        {
            await StopAsync(dock);
            await Task.Delay(200);
        }
    }

    // A screen reader keeps its place when the list changes: the element for an app that only moved stays alive,
    // the one for an app that left the dock says it is gone.
    [TestMethod]
    public async Task ScreenReaderElementsSurviveAMoveAndGoWithTheirApp()
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
                new DockItem(DockItemKind.Path, @"C:\Harbor test\leaving.exe", "Leaving", null),
                new DockItem(DockItemKind.Path, @"C:\Harbor test\staying.exe", "Staying", null),
            ])));
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "island: 2 items", TimeSpan.FromSeconds(10)));
            var island = IslandWindow.Find(dock.Id);
            using var staying = await TestDock.HoldUiaElementAsync(island, "Staying");
            using var leaving = await TestDock.HoldUiaElementAsync(island, "Leaving");

            // Staying moves to the front; Leaving goes.
            Assert.IsTrue(server.Server.TrySend(new DockItems(
            [
                new DockItem(DockItemKind.Path, @"C:\Harbor test\staying.exe", "Staying", null),
                new DockItem(DockItemKind.Path, @"C:\Harbor test\new.exe", "New", null),
                new DockItem(DockItemKind.Path, @"C:\Harbor test\other.exe", "Other", null),
            ])));
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "island: 3 items", TimeSpan.FromSeconds(10)));

            var (stayingExit, stayingOutput) = await TestDock.ReleaseUiaElementAsync(staying);
            Assert.AreEqual(0, stayingExit, stayingOutput);
            StringAssert.Contains(stayingOutput, "alive\tStaying");
            var (leavingExit, leavingOutput) = await TestDock.ReleaseUiaElementAsync(leaving);
            Assert.AreEqual(5, leavingExit, leavingOutput);
        }
        finally
        {
            await StopAsync(dock);
        }
    }

    // The dock follows app windows as they come, change app, hide and go, grouped by app, and only the test's
    // own windows (the user's are there too). Its log says how many, never which.
    [TestMethod]
    public async Task TheDockFollowsAppWindowsGroupedByApp()
    {
        await using var server = new RunningServer($"harbor.e2e.{Guid.NewGuid():N}", TimeSpan.FromMilliseconds(100));
        using var dataDir = new TempDirectory();
        using var taskbar = new FakeTaskbar(dataDir.Path);
        using var dock = TestDock.Start(server.PipeName, dataDir.Path, taskbar);
        using var windows = new TestWindows(taskbar.Id);
        var aumid = $"Harbor.Test.{taskbar.Id}.Grouped";
        var timeout = TimeSpan.FromSeconds(10);
        try
        {
            await server.WaitForAsync(s => s.Connected);
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "windows: 0 apps, 0 windows", timeout));

            async Task ExpectAsync(string command, string counts)
            {
                var from = await TestDock.LogLengthAsync(dataDir.Path);
                await windows.SendAsync(command);
                Assert.IsTrue(await TestDock.WaitForLogAfterAsync(dataDir.Path, from, $"windows: {counts}", timeout),
                    $"after {command}: {await TestDock.ReadLogAsync(dataDir.Path)}");
            }

            await ExpectAsync($"new 1 {aumid}", "1 apps, 1 windows");
            await ExpectAsync($"new 2 {aumid}", "1 apps, 2 windows");
            // No AppUserModelID: the helper's executable is the app.
            await ExpectAsync("new 3", "2 apps, 3 windows");
            // Set late: it joins the others.
            await ExpectAsync($"aumid 3 {aumid}", "1 apps, 3 windows");
            // A window its own app cloaked stays (only the shell's cloaking, as for other desktops, hides one);
            // the hidden one goes.
            await windows.SendAsync("cloak 1");
            await ExpectAsync("hide 3", "1 apps, 2 windows");
            await ExpectAsync("close 1", "1 apps, 1 windows");
            await ExpectAsync("close 2", "0 apps, 0 windows");

            Assert.IsFalse((await TestDock.ReadLogAsync(dataDir.Path)).Contains(".Grouped", StringComparison.Ordinal),
                "the log names an app");
        }
        finally
        {
            await StopAsync(dock);
        }
    }

    // Running apps get a dot: a pinned one on its pin (here by AppUserModelID, without waiting for the Shell),
    // the others as items after the pinned ones, under their file name once the Shell had its chance to name
    // them. Screen readers hear how many windows each has.
    [TestMethod]
    public async Task RunningAppsShowOnTheirPinOrAfterThePinnedItems()
    {
        const int ToolBar = 50021;
        const int Button = 50000;
        await using var server = new RunningServer($"harbor.e2e.{Guid.NewGuid():N}", TimeSpan.FromMilliseconds(100));
        using var dataDir = new TempDirectory();
        using var taskbar = new FakeTaskbar(dataDir.Path);
        using var dock = TestDock.Start(server.PipeName, dataDir.Path, taskbar);
        using var windows = new TestWindows(taskbar.Id);
        var pinned = $"Harbor.Test.{taskbar.Id}.Pinned";
        var timeout = TimeSpan.FromSeconds(10);
        try
        {
            await server.WaitForAsync(s => s.Connected);
            Assert.IsTrue(server.Server.TrySend(new DockItems(
            [
                new DockItem(DockItemKind.Path, @"C:\Harbor test\idle.exe", "Idle", null),
                new DockItem(DockItemKind.App, pinned, "Pinned app", null),
            ])));
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "island: 2 items", timeout));

            await windows.SendAsync($"new 1 {pinned}");
            await windows.SendAsync("new 2");
            await windows.SendAsync("new 3");
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "island: 3 items", timeout),
                await TestDock.ReadLogAsync(dataDir.Path));
            var island = IslandWindow.Find(dock.Id);

            var (listed, tree) = await TestDock.RunUiaClientAsync(island, "list");
            Assert.AreEqual(0, listed, tree);
            CollectionAssert.AreEqual(
                new[] { $"{ToolBar}\tHarbor Dock", $"{Button}\tIdle", $"{Button}\tPinned app", $"{Button}\tharbor_test_windows" },
                tree.Split(['\r', '\n'], StringSplitOptions.RemoveEmptyEntries), tree);
            async Task<string> StatusAsync(string name) =>
                (await TestDock.RunUiaClientAsync(island, "status", name)).Output.TrimEnd('\r', '\n');
            Assert.AreEqual("status\t", await StatusAsync("Idle"));
            Assert.AreEqual("status\trunning, 1 window", await StatusAsync("Pinned app"));
            Assert.AreEqual("status\trunning, 2 windows", await StatusAsync("harbor_test_windows"));

            // Their windows gone, the running app's item goes; the pinned items stay.
            var from = await TestDock.LogLengthAsync(dataDir.Path);
            await windows.SendAsync("close 1");
            await windows.SendAsync("close 2");
            await windows.SendAsync("close 3");
            Assert.IsTrue(await TestDock.WaitForLogAfterAsync(dataDir.Path, from, "island: 2 items", timeout));
            Assert.AreEqual("status\t", await StatusAsync("Pinned app"));
        }
        finally
        {
            await StopAsync(dock);
        }
    }

    // The dock asks Harbor Shell about each app it sees running (appLookup) and shows the answer (appInfo): its
    // name and icon, and, when the Shell says the app is a pinned item, its dot on that item instead.
    [TestMethod]
    public async Task HarborShellNamesRunningAppsAndMatchesThemToPins()
    {
        const int Button = 50000;
        await using var server = new RunningServer($"harbor.e2e.{Guid.NewGuid():N}", TimeSpan.FromMilliseconds(100));
        using var dataDir = new TempDirectory();
        using var taskbar = new FakeTaskbar(dataDir.Path);
        using var dock = TestDock.Start(server.PipeName, dataDir.Path, taskbar);
        using var windows = new TestWindows(taskbar.Id);
        var aumid = $"Harbor.Test.{taskbar.Id}.Named";
        var icon = new IconStore(dataDir.Path, new CapturingLog()).Save(new IconImage(1, 1, [255, 0, 0, 255]))!;
        var timeout = TimeSpan.FromSeconds(10);
        try
        {
            await server.WaitForAsync(s => s.Connected);
            await windows.SendAsync($"new 1 {aumid}");
            var lookup = await server.WaitForMessageAsync<AppLookup>(l => l.Aumid == aumid);
            StringAssert.EndsWith(lookup.Exe, "harbor_test_windows.exe");
            Assert.IsNotNull(lookup.Window);

            Assert.IsTrue(server.Server.TrySend(new AppInfo(lookup.Id, "Named by the Shell", icon, null, new AppTarget(DockItemKind.App, aumid))));
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "island: 1 items (1 icons)", timeout),
                await TestDock.ReadLogAsync(dataDir.Path));
            var island = IslandWindow.Find(dock.Id);
            var (_, tree) = await TestDock.RunUiaClientAsync(island, "list");
            CollectionAssert.Contains(tree.Split(['\r', '\n'], StringSplitOptions.RemoveEmptyEntries), $"{Button}\tNamed by the Shell");

            // A pin only the Shell can match (a shortcut, say): the app joins it once the Shell says so, in the
            // order the messages came.
            var from = await TestDock.LogLengthAsync(dataDir.Path);
            var pin = new AppTarget(DockItemKind.Path, @"C:\Harbor test\Named.lnk");
            Assert.IsTrue(server.Server.TrySend(new DockItems([new DockItem(pin.Kind, pin.Target, "Pinned", null)])));
            Assert.IsTrue(server.Server.TrySend(new AppInfo(lookup.Id, "Named by the Shell", icon, pin, null)));
            Assert.IsTrue(await TestDock.WaitForLogAfterAsync(dataDir.Path, from, "island: 1 items (0 icons)", timeout),
                await TestDock.ReadLogAsync(dataDir.Path));
            Assert.AreEqual("status\trunning, 1 window",
                (await TestDock.RunUiaClientAsync(island, "status", "Pinned")).Output.TrimEnd('\r', '\n'));
            Assert.IsFalse((await TestDock.ReadLogAsync(dataDir.Path)).Contains(".Named", StringComparison.Ordinal),
                "the log names an app");
        }
        finally
        {
            await StopAsync(dock);
        }
    }

    // A click on a running app (here UI Automation's Invoke, which does the same) brings its most recent window
    // forward; again, its next window in turn; with one window in front, minimizes it; and brings a minimized
    // one back.
    [TestMethod]
    public async Task ClickingARunningAppCyclesItsWindowsAndMinimizesTheOnlyOne()
    {
        TestDock.RequireForegroundTests();
        await using var server = new RunningServer($"harbor.e2e.{Guid.NewGuid():N}", TimeSpan.FromMilliseconds(100));
        using var dataDir = new TempDirectory();
        using var taskbar = new FakeTaskbar(dataDir.Path);
        using var dock = TestDock.Start(server.PipeName, dataDir.Path, taskbar);
        using var windows = new TestWindows(taskbar.Id);
        var timeout = TimeSpan.FromSeconds(10);
        try
        {
            await server.WaitForAsync(s => s.Connected);
            await windows.SendAsync("new 1");
            await windows.SendAsync("new 2");
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "island: 1 items", timeout),
                await TestDock.ReadLogAsync(dataDir.Path));
            var island = IslandWindow.Find(dock.Id);

            async Task ClickAsync()
            {
                var (code, output) = await TestDock.RunUiaClientAsync(island, "invoke", "harbor_test_windows");
                Assert.AreEqual(0, code, output);
            }
            Task<bool> InFrontAsync(int window) => TestDock.WaitUntilAsync(
                async () => await windows.SendAsync("foreground") == $"ok {window}", timeout);
            Task<bool> MinimizedAsync(int window, bool minimized) => TestDock.WaitUntilAsync(
                async () => await windows.SendAsync($"minimized {window}") == (minimized ? "ok 1" : "ok 0"), timeout);

            // Neither was in front yet: the newer one comes forward.
            await ClickAsync();
            Assert.IsTrue(await InFrontAsync(2), "window 2 didn't come to the front");
            await ClickAsync();
            Assert.IsTrue(await InFrontAsync(1), "window 1 didn't come next");
            await ClickAsync();
            Assert.IsTrue(await InFrontAsync(2), "the cycle didn't start over");

            var from = await TestDock.LogLengthAsync(dataDir.Path);
            await windows.SendAsync("close 1");
            Assert.IsTrue(await TestDock.WaitForLogAfterAsync(dataDir.Path, from, "windows: 1 apps, 1 windows", timeout));
            await ClickAsync();
            Assert.IsTrue(await MinimizedAsync(2, true), "the only window in front wasn't minimized");
            await ClickAsync();
            Assert.IsTrue(await MinimizedAsync(2, false), "the minimized window wasn't restored");
            Assert.IsTrue(await InFrontAsync(2), "the restored window isn't in front");

            var log = await TestDock.ReadLogAsync(dataDir.Path);
            StringAssert.Contains(log, "click: item 0 (UI Automation): its window is minimized");
            StringAssert.Contains(log, "click: item 0 (UI Automation): a window comes forward, restored");
        }
        finally
        {
            await StopAsync(dock);
        }
    }

    // The right-click menu: the app's name, its windows by title, and what can be done; screen readers read it
    // (owner-drawn items carry their text for them). "Close all windows" asks each window to close.
    [TestMethod]
    public async Task TheMenuListsAnAppsWindowsAndClosesThemAll()
    {
        TestDock.RequireForegroundTests();
        const int MenuItem = 50011;
        await using var server = new RunningServer($"harbor.e2e.{Guid.NewGuid():N}", TimeSpan.FromMilliseconds(100));
        using var dataDir = new TempDirectory();
        using var taskbar = new FakeTaskbar(dataDir.Path);
        using var dock = TestDock.Start(server.PipeName, dataDir.Path, taskbar);
        using var windows = new TestWindows(taskbar.Id);
        var timeout = TimeSpan.FromSeconds(10);
        try
        {
            await server.WaitForAsync(s => s.Connected);
            await windows.SendAsync("new 1");
            await windows.SendAsync("new 2");
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "island: 1 items", timeout));
            var island = IslandWindow.Find(dock.Id);

            var from = await TestDock.LogLengthAsync(dataDir.Path);
            TestDock.OpenMenu(taskbar);
            var (code, items) = await TestDock.UseMenuAsync(island, "Close all windows");
            Assert.AreEqual(0, code, string.Join("\n", items));
            var names = items.Where(item => item.StartsWith($"{MenuItem}\t", StringComparison.Ordinal)).Select(item => item[6..]).ToArray();
            CollectionAssert.AreEqual(new[] { "harbor_test_windows", "Test 1", "Test 2", "Close all windows" }, names, string.Join("\n", items));

            Assert.IsTrue(await TestDock.WaitForLogAfterAsync(dataDir.Path, from, "windows: 0 apps, 0 windows", timeout),
                await TestDock.ReadLogAsync(dataDir.Path));
            var log = await TestDock.ReadLogAsync(dataDir.Path);
            StringAssert.Contains(log, "menu: item 0 (test hook): close all its windows");
            Assert.IsFalse(log.Contains("Test 1", StringComparison.Ordinal), "the log names a window");
        }
        finally
        {
            await StopAsync(dock);
        }
    }

    // "Pin to dock" asks Harbor Shell (pinRequest) to pin what the Shell said starts the app; the dock itself
    // changes nothing until the new list comes.
    [TestMethod]
    public async Task PinningFromTheMenuAsksHarborShell()
    {
        TestDock.RequireForegroundTests();
        await using var server = new RunningServer($"harbor.e2e.{Guid.NewGuid():N}", TimeSpan.FromMilliseconds(100));
        using var dataDir = new TempDirectory();
        using var taskbar = new FakeTaskbar(dataDir.Path);
        using var dock = TestDock.Start(server.PipeName, dataDir.Path, taskbar);
        using var windows = new TestWindows(taskbar.Id);
        var aumid = $"Harbor.Test.{taskbar.Id}.Pinnable";
        try
        {
            await server.WaitForAsync(s => s.Connected);
            await windows.SendAsync($"new 1 {aumid}");
            var lookup = await server.WaitForMessageAsync<AppLookup>(l => l.Aumid == aumid);
            var launch = new AppTarget(DockItemKind.App, aumid);
            Assert.IsTrue(server.Server.TrySend(new AppInfo(lookup.Id, "Pinnable", null, null, launch)));
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "island: 1 items", TimeSpan.FromSeconds(10)));
            var island = IslandWindow.Find(dock.Id);

            TestDock.OpenMenu(taskbar);
            var (code, items) = await TestDock.UseMenuAsync(island, "Pin to dock");
            Assert.AreEqual(0, code, string.Join("\n", items));
            var request = await server.WaitForMessageAsync<PinRequest>(_ => true);
            Assert.AreEqual(new PinRequest(PinAction.Pin, launch), request);
        }
        finally
        {
            await StopAsync(dock);
        }
    }

    // An open menu runs its own loop on the island's thread; pongs (proof the thread is alive) must keep coming
    // while it stays open, and the panic hotkey closes it.
    [TestMethod]
    public async Task PongsKeepComingWhileAMenuIsOpenAndPanicClosesIt()
    {
        TestDock.RequireForegroundTests();
        await using var server = new RunningServer($"harbor.e2e.{Guid.NewGuid():N}", TimeSpan.FromMilliseconds(100));
        using var dataDir = new TempDirectory();
        using var taskbar = new FakeTaskbar(dataDir.Path);
        using var dock = TestDock.Start(server.PipeName, dataDir.Path, taskbar);
        using var windows = new TestWindows(taskbar.Id);
        try
        {
            await server.WaitForAsync(s => s.Connected);
            await windows.SendAsync("new 1");
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "island: 1 items", TimeSpan.FromSeconds(10)));
            var island = IslandWindow.Find(dock.Id);

            TestDock.OpenMenu(taskbar);
            Assert.AreEqual(0, (await TestDock.UseMenuAsync(island)).ExitCode, "the menu didn't open");
            var before = (await server.WaitForAsync(s => s.LastPongSeq is not null)).LastPongSeq!.Value;
            await Task.Delay(TimeSpan.FromSeconds(12));
            var after = (await server.WaitForAsync(s => s.LastPongSeq > before + 50)).LastPongSeq!.Value;
            Assert.IsTrue(after > before + 50, $"pongs {before} -> {after}");
            Assert.AreEqual(0, (await TestDock.UseMenuAsync(island)).ExitCode, "the menu closed by itself");

            TestDock.PressPanicHotkey(server.PipeName);
            Assert.AreEqual(6, (await TestDock.UseMenuAsync(island)).ExitCode, "the panic hotkey left the menu open");
        }
        finally
        {
            await StopAsync(dock);
        }
    }

    // WM_CLOSE (what Harbor Shell's "Stop dock" sends) is a normal exit.
    [TestMethod]
    public async Task TheDockExitsNormallyWhenItsWindowIsClosed()
    {
        await using var server = new RunningServer($"harbor.e2e.{Guid.NewGuid():N}", TimeSpan.FromMilliseconds(100));
        using var dataDir = new TempDirectory();
        using var taskbar = new FakeTaskbar(dataDir.Path);
        using var dock = TestDock.Start(server.PipeName, dataDir.Path, taskbar);
        try
        {
            Assert.IsTrue(await TestDock.WaitForLogAsync(dataDir.Path, "island: shown", TimeSpan.FromSeconds(15)));
            IslandWindow.Close(IslandWindow.Find(dock.Id));

            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(15));
            await dock.WaitForExitAsync(timeout.Token);
            Assert.AreEqual(0, dock.ExitCode);
            var log = await TestDock.ReadLogAsync(dataDir.Path);
            StringAssert.Contains(log, "asked to close; exiting");
            StringAssert.Contains(log, "harbor-dock exiting with code 0");
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
