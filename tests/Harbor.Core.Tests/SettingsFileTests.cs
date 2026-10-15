using System.Text;
using Harbor.Core.Settings;

namespace Harbor.Core.Tests;

[TestClass]
public sealed class SettingsFileTests
{
    [TestMethod]
    public async Task CreatesTheDefaultsTheFirstTime()
    {
        using var dataDir = new TempDirectory();
        var log = new CapturingLog();
        // The data directory itself may not exist yet.
        var file = new SettingsFile(Path.Combine(dataDir.Path, "Harbor"), log);

        var result = await file.ReadAsync();

        Assert.AreEqual(HarborSettings.Defaults, result.Settings);
        Assert.IsNull(result.Problem);
        Assert.IsTrue(HarborSettings.TryDecode(File.ReadAllBytes(file.FilePath), out var written, out _));
        Assert.AreEqual(HarborSettings.Defaults, written);
        Assert.HasCount(1, Directory.GetFiles(file.DataDir), "the temporary file was left behind");
        Assert.IsTrue(log.Contains("created"));
    }

    [TestMethod]
    public async Task ReadsTheUsersSettings()
    {
        using var dataDir = new TempDirectory();
        var settings = new HarborSettings([new Pin(PinKind.Path, @"C:\Tools\app.exe")]);
        File.WriteAllBytes(HarborSettings.PathIn(dataDir.Path), HarborSettings.Encode(settings));

        var result = await new SettingsFile(dataDir.Path, new CapturingLog()).ReadAsync();

        Assert.AreEqual(settings, result.Settings);
    }

    // The user's edits are worth more than the defaults: a broken file is reported, never replaced.
    [TestMethod]
    public async Task NeverOverwritesABrokenFile()
    {
        using var dataDir = new TempDirectory();
        var path = HarborSettings.PathIn(dataDir.Path);
        var broken = Encoding.UTF8.GetBytes("""{"v":1,"pinned":[{"app":"MSEdge"},]}""");
        File.WriteAllBytes(path, broken);

        var result = await new SettingsFile(dataDir.Path, new CapturingLog()).ReadAsync();

        Assert.IsNull(result.Settings);
        StringAssert.Contains(result.Problem, "isn't valid JSON");
        CollectionAssert.AreEqual(broken, File.ReadAllBytes(path));
    }

    [TestMethod]
    public async Task ReadsAFileAnEditorHasOpen()
    {
        using var dataDir = new TempDirectory();
        var path = HarborSettings.PathIn(dataDir.Path);
        File.WriteAllBytes(path, HarborSettings.Encode(HarborSettings.Defaults));
        using var editor = new FileStream(path, FileMode.Open, FileAccess.ReadWrite, FileShare.ReadWrite);

        var result = await new SettingsFile(dataDir.Path, new CapturingLog()).ReadAsync();

        Assert.AreEqual(HarborSettings.Defaults, result.Settings);
    }

    [TestMethod]
    public async Task ReportsAFileItCantRead()
    {
        using var dataDir = new TempDirectory();
        var path = HarborSettings.PathIn(dataDir.Path);
        File.WriteAllBytes(path, HarborSettings.Encode(HarborSettings.Defaults));
        var log = new CapturingLog();
        using (new FileStream(path, FileMode.Open, FileAccess.ReadWrite, FileShare.None))
        {
            var result = await new SettingsFile(dataDir.Path, log).ReadAsync();

            Assert.IsNull(result.Settings);
            StringAssert.Contains(result.Problem, "can't be read");
        }
        Assert.IsTrue(log.Contains("trying again"), "a locked file is retried before giving up");
    }

    [TestMethod]
    public async Task RefusesAHugeFile()
    {
        using var dataDir = new TempDirectory();
        File.WriteAllBytes(HarborSettings.PathIn(dataDir.Path), new byte[(1024 * 1024) + 1]);

        var result = await new SettingsFile(dataDir.Path, new CapturingLog()).ReadAsync();

        StringAssert.Contains(result.Problem, "larger than 1024 KiB");
    }
}
