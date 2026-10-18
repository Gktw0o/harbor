using Harbor.Core.Apps;

namespace Harbor.Core.Tests;

[TestClass]
public sealed class AppIndexTests
{
    private sealed class CountingSource(params AppEntry[] apps) : IAppSource
    {
        public int Reads { get; private set; }

        public IReadOnlyList<AppEntry> ReadAll()
        {
            Reads++;
            return apps;
        }
    }

    [TestMethod]
    public async Task ReadsTheListThenSearchesIt()
    {
        var source = new CountingSource(new AppEntry("Calculator", "Calc!App"), new AppEntry("Paint", "Paint!App"));
        var index = new AppIndex(source, new CapturingLog());
        var changed = 0;
        index.Changed += () => changed++;
        Assert.AreEqual(0, index.Search.Count);

        await index.RefreshAsync();
        Assert.AreEqual(2, index.Search.Count);
        Assert.AreEqual("Calc!App", index.Search.Find("calc", 1)[0].Id);
        Assert.AreEqual(1, changed);
    }

    // The island asks again every time it opens: a fresh list isn't read again unless asked to.
    [TestMethod]
    public async Task AFreshListIsntReadAgainUnlessForced()
    {
        var source = new CountingSource(new AppEntry("Calculator", "Calc!App"));
        var index = new AppIndex(source, new CapturingLog());
        await index.RefreshAsync();
        await index.RefreshAsync();
        Assert.AreEqual(1, source.Reads);
        await index.RefreshAsync(force: true);
        Assert.AreEqual(2, source.Reads);
    }

    [TestMethod]
    public void ATestListComesFromItsFile()
    {
        using var dir = new TempDirectory();
        var path = Path.Combine(dir.Path, FileAppSource.FileName);
        File.WriteAllText(path, """[{"name": "Marker", "id": "C:\\Tools\\marker.exe"}, {"name": "", "id": "skipped"}]""");
        var apps = new FileAppSource(path).ReadAll();
        Assert.HasCount(1, apps);
        Assert.AreEqual(new AppEntry("Marker", @"C:\Tools\marker.exe"), apps[0]);
        Assert.IsEmpty(new FileAppSource(Path.Combine(dir.Path, "none.json")).ReadAll());
    }

    [TestMethod]
    public async Task ABrokenListIsLoggedAndTheOldOneKept()
    {
        using var dir = new TempDirectory();
        var path = Path.Combine(dir.Path, FileAppSource.FileName);
        File.WriteAllText(path, """[{"name": "Marker", "id": "C:\\m.exe"}]""");
        var log = new CapturingLog();
        var index = new AppIndex(new FileAppSource(path), log);
        await index.RefreshAsync();
        File.WriteAllText(path, "not json");
        await index.RefreshAsync(force: true);
        Assert.AreEqual(1, index.Search.Count);
        Assert.IsTrue(log.Contains("couldn't read the list"), string.Join(Environment.NewLine, log.Lines));
    }
}
