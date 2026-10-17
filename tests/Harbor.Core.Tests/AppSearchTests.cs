using System.Diagnostics;
using Harbor.Core.Apps;
using Harbor.Core.Ipc;

namespace Harbor.Core.Tests;

[TestClass]
public sealed class AppSearchTests
{
    private static AppSearch Search(params string[] names) => new(names.Select((name, i) => new AppEntry(name, $"App{i}")));

    private static string[] Names(IReadOnlyList<AppEntry> apps) => [.. apps.Select(app => app.Name)];

    [TestMethod]
    public void TheNameStartingWithItComesFirstThenAWordThenInitialsThenAnywhere()
    {
        var search = Search("Code Insights", "Visual Studio Code", "Barcode Reader", "Character Map", "Codex", "VS Community");
        CollectionAssert.AreEqual(new[] { "Codex", "Code Insights", "Visual Studio Code", "Barcode Reader" }, Names(search.Find("code", 10)));
        CollectionAssert.AreEqual(new[] { "Visual Studio Code" }, Names(search.Find("vsc", 10)));
        // A shorter name first among equals.
        CollectionAssert.AreEqual(new[] { "VS Community", "Visual Studio Code" }, Names(search.Find("v", 2)));
    }

    [TestMethod]
    public void CaseAccentsAndPunctuationDontMatter()
    {
        var search = Search("Mühendis Notları", "Café-Bar", "Paint 3D");
        CollectionAssert.AreEqual(new[] { "Mühendis Notları" }, Names(search.Find("MUHENDIS", 10)));
        CollectionAssert.AreEqual(new[] { "Café-Bar" }, Names(search.Find("cafe bar", 10)));
        CollectionAssert.AreEqual(new[] { "Paint 3D" }, Names(search.Find("3d", 10)));
    }

    // Turkish: ı/I and i/İ are different letters, but typed without care (or on another keyboard) they must find
    // each other.
    [TestMethod]
    public void TheTurkishDottedAndDotlessIFindEachOther()
    {
        var search = Search("Ayarlar", "Dosya Gezgini", "İnternet Bağlantısı", "Işık Ayarı");
        CollectionAssert.AreEqual(new[] { "İnternet Bağlantısı" }, Names(search.Find("internet", 10)));
        CollectionAssert.AreEqual(new[] { "İnternet Bağlantısı" }, Names(search.Find("İNTERNET", 10)));
        CollectionAssert.AreEqual(new[] { "İnternet Bağlantısı" }, Names(search.Find("baglantisi", 10)));
        CollectionAssert.AreEqual(new[] { "Işık Ayarı" }, Names(search.Find("isik", 10)));
        CollectionAssert.AreEqual(new[] { "Işık Ayarı" }, Names(search.Find("ışık", 10)));
        Assert.AreEqual("i", AppSearch.Fold("I"));
        Assert.AreEqual("i", AppSearch.Fold("İ"));
        Assert.AreEqual("i", AppSearch.Fold("ı"));
    }

    [TestMethod]
    public void NothingForAnEmptyQueryOrNoMatch()
    {
        var search = Search("Calculator");
        Assert.IsEmpty(search.Find("", 10));
        Assert.IsEmpty(search.Find("   ", 10));
        Assert.IsEmpty(search.Find("xyz", 10));
        Assert.IsEmpty(search.Find("calc", 0));
    }

    [TestMethod]
    public void AnAbsolutePathStartsAsAPathAnythingElseThroughTheAppsFolder()
    {
        Assert.AreEqual(new AppTarget(DockItemKind.App, "Microsoft.WindowsCalculator_8wekyb3d8bbwe!App"),
            new AppEntry("Calculator", "Microsoft.WindowsCalculator_8wekyb3d8bbwe!App").Target);
        Assert.AreEqual(new AppTarget(DockItemKind.App, @"{6D809377-6AF0-444B-8957-A3773F02200E}\Tool\tool.exe"),
            new AppEntry("Tool", @"{6D809377-6AF0-444B-8957-A3773F02200E}\Tool\tool.exe").Target);
        Assert.AreEqual(new AppTarget(DockItemKind.Path, @"C:\Tools\tool.exe"), new AppEntry("Tool", @"C:\Tools\tool.exe").Target);
    }

    // docs/adr/0016: a query over 2,000 apps in under 2 ms, as the user types.
    [TestMethod]
    public void TwoThousandAppsAreSearchedInUnderTwoMilliseconds()
    {
        var words = new[] { "Microsoft", "Visual", "Studio", "Code", "Office", "Word", "Excel", "Paint", "Notepad", "Terminal", "Ayarlar", "Gezgini", "İnternet", "Işık", "Player", "Editor", "Manager", "Tools" };
        var random = new Random(4);
        var apps = Enumerable.Range(0, 2000)
            .Select(i => new AppEntry(string.Join(' ', Enumerable.Range(0, 3).Select(_ => words[random.Next(words.Length)])) + $" {i}", $"App{i}"))
            .ToList();
        var search = new AppSearch(apps);
        var queries = new[] { "v", "vi", "vis", "visual s", "code", "ex", "ışık", "msc", "tool 19", "zz" };
        foreach (var query in queries)
        {
            search.Find(query, 8);
        }
        var watch = Stopwatch.StartNew();
        const int Rounds = 20;
        for (var round = 0; round < Rounds; round++)
        {
            foreach (var query in queries)
            {
                search.Find(query, 8);
            }
        }
        var perQuery = watch.Elapsed.TotalMilliseconds / (Rounds * queries.Length);
        Assert.IsLessThan(2.0, perQuery, $"{perQuery:F3} ms per query");
    }
}
