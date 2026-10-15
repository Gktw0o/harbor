using System.Text;
using Harbor.Core.Ipc;
using Harbor.Core.Settings;

namespace Harbor.Core.Tests;

[TestClass]
public sealed class PinChangesTests
{
    private static readonly Pin Terminal = new(PinKind.App, "Microsoft.WindowsTerminal_8wekyb3d8bbwe!App");
    private static readonly Pin Tool = new(PinKind.Path, @"C:\Tools\tool.exe");

    [TestMethod]
    public void PinningAddsAtTheEndOnce()
    {
        var settings = new HarborSettings([Terminal]);

        var pinned = PinChanges.Apply(settings, PinAction.Pin, Tool);
        Assert.AreEqual(new HarborSettings([Terminal, Tool]), pinned.Settings);

        // Already there, whatever the case: nothing to write.
        var again = PinChanges.Apply(pinned.Settings!, PinAction.Pin, Tool with { Target = Tool.Target.ToUpperInvariant() });
        Assert.IsNull(again.Settings);
        Assert.AreEqual("it is pinned already", again.Outcome);
    }

    [TestMethod]
    public void UnpinningTakesEveryCopyAway()
    {
        var settings = new HarborSettings([Tool, Terminal, Tool with { Target = @"c:\tools\TOOL.exe" }]);

        Assert.AreEqual(new HarborSettings([Terminal]), PinChanges.Apply(settings, PinAction.Unpin, Tool).Settings);
        Assert.IsNull(PinChanges.Apply(new HarborSettings([Terminal]), PinAction.Unpin, Tool).Settings);
    }

    [TestMethod]
    public void AFullDockOrAPinTheFileCantHoldIsRefused()
    {
        var full = new HarborSettings([.. Enumerable.Range(0, HarborSettings.MaxPinned).Select(i => new Pin(PinKind.App, $"App.{i}"))]);
        var refused = PinChanges.Apply(full, PinAction.Pin, Tool);
        Assert.IsNull(refused.Settings);
        StringAssert.Contains(refused.Outcome, "holds 40");

        // Paths must be absolute.
        var relative = PinChanges.Apply(new HarborSettings([]), PinAction.Pin, new Pin(PinKind.Path, @"tools\tool.exe"));
        Assert.IsNull(relative.Settings);
        StringAssert.StartsWith(relative.Outcome, "it can't be pinned");
    }

    [TestMethod]
    public async Task APinRequestRewritesTheFileAtomically()
    {
        using var dataDir = new TempDirectory();
        var file = new SettingsFile(dataDir.Path, new CapturingLog());
        File.WriteAllBytes(file.FilePath, HarborSettings.Encode(new HarborSettings([Terminal])));
        await file.ReadAsync();
        var log = new CapturingLog();

        var result = await PinChanges.ApplyAsync(file, new PinRequest(PinAction.Pin, new AppTarget(DockItemKind.Path, Tool.Target)), log);

        Assert.IsTrue(result.Written, result.Outcome);
        Assert.IsTrue(HarborSettings.TryDecode(File.ReadAllBytes(file.FilePath), out var written, out _));
        Assert.AreEqual(new HarborSettings([Terminal, Tool]), written);
        Assert.HasCount(1, Directory.GetFiles(dataDir.Path), "a temporary file was left behind");
        Assert.IsTrue(log.Contains("pin request from the dock: pinned"));
        Assert.IsFalse(log.Contains("tool.exe"), "the log names the app");

        // Its own write counts as read: a second change needn't wait for the watcher.
        var unpinned = await PinChanges.ApplyAsync(file, new PinRequest(PinAction.Unpin, new AppTarget(DockItemKind.App, Terminal.Target)), log);
        Assert.IsTrue(unpinned.Written, unpinned.Outcome);
    }

    // The user may have edited it since Harbor read it: their edit wins, and the request is refused.
    [TestMethod]
    public async Task AFileEditedSinceItWasReadIsLeftAlone()
    {
        using var dataDir = new TempDirectory();
        var file = new SettingsFile(dataDir.Path, new CapturingLog());
        File.WriteAllBytes(file.FilePath, HarborSettings.Encode(new HarborSettings([Terminal])));
        await file.ReadAsync();
        var edited = HarborSettings.Encode(new HarborSettings([Terminal, Tool]));
        File.WriteAllBytes(file.FilePath, edited);

        var result = await PinChanges.ApplyAsync(file, new PinRequest(PinAction.Unpin, new AppTarget(DockItemKind.App, Terminal.Target)), new CapturingLog());

        Assert.IsFalse(result.Written);
        StringAssert.Contains(result.Outcome, "changed since Harbor read it");
        CollectionAssert.AreEqual(edited, File.ReadAllBytes(file.FilePath));
    }

    [TestMethod]
    public async Task ABrokenFileIsLeftAlone()
    {
        using var dataDir = new TempDirectory();
        var file = new SettingsFile(dataDir.Path, new CapturingLog());
        var broken = Encoding.UTF8.GetBytes("""{"v":1,"pinned":[{"app":"MSEdge"},]}""");
        File.WriteAllBytes(file.FilePath, broken);
        await file.ReadAsync();

        var result = await PinChanges.ApplyAsync(file, new PinRequest(PinAction.Pin, new AppTarget(DockItemKind.Path, Tool.Target)), new CapturingLog());

        Assert.IsFalse(result.Written);
        CollectionAssert.AreEqual(broken, File.ReadAllBytes(file.FilePath));
    }

    // The defaults Harbor wrote itself count as read.
    [TestMethod]
    public async Task TheDefaultsCanBeChangedRightAway()
    {
        using var dataDir = new TempDirectory();
        var file = new SettingsFile(dataDir.Path, new CapturingLog());
        await file.ReadAsync();

        var result = await PinChanges.ApplyAsync(file, new PinRequest(PinAction.Pin, new AppTarget(DockItemKind.Path, Tool.Target)), new CapturingLog());

        Assert.IsTrue(result.Written, result.Outcome);
    }
}
