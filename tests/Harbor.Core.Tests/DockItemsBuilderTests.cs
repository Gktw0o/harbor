using Harbor.Core.Apps;
using Harbor.Core.Ipc;
using Harbor.Core.Settings;

namespace Harbor.Core.Tests;

[TestClass]
public sealed class DockItemsBuilderTests
{
    // Answers from a table; a target that maps to an exception throws it.
    private sealed class FakeResolver(Dictionary<string, object?> answers) : IPinResolver
    {
        public ResolvedPin? Resolve(Pin pin) => answers.GetValueOrDefault(pin.Target) switch
        {
            Exception exception => throw exception,
            var answer => (ResolvedPin?)answer,
        };
    }

    private static DockItems Build(HarborSettings settings, Dictionary<string, object?> answers, CapturingLog? log = null) =>
        DockItemsBuilder.Build(settings, new FakeResolver(answers), log ?? new CapturingLog());

    [TestMethod]
    public void KeepsTheOrderAndLeavesOffWhatIsntInstalled()
    {
        var log = new CapturingLog();
        var items = Build(
            new HarborSettings([new Pin(PinKind.App, "A"), new Pin(PinKind.App, "Missing"), new Pin(PinKind.Path, @"C:\b.exe")]),
            new() { ["A"] = new ResolvedPin("App A", "0123456789abcdef.bgra"), [@"C:\b.exe"] = new ResolvedPin("B", null) },
            log);

        Assert.AreEqual(
            new DockItems(
            [
                new DockItem(DockItemKind.App, "A", "App A", "0123456789abcdef.bgra"),
                new DockItem(DockItemKind.Path, @"C:\b.exe", "B", null),
            ]),
            items);
        Assert.IsTrue(log.Contains("pinned[1] isn't installed"));
        Assert.IsFalse(log.Contains("Missing"), "the log names the user's app");
    }

    [TestMethod]
    public void ALookupThatFailsLeavesOnlyThatAppOff()
    {
        var items = Build(
            new HarborSettings([new Pin(PinKind.App, "Broken"), new Pin(PinKind.App, "A")]),
            new() { ["Broken"] = new InvalidOperationException("boom"), ["A"] = new ResolvedPin("A", null) });

        Assert.HasCount(1, items.Items);
        Assert.AreEqual("A", items.Items[0].Target);
    }

    [TestMethod]
    public void FallsBackWhenWindowsHasNoName()
    {
        var items = Build(
            new HarborSettings([new Pin(PinKind.Path, @"C:\Tools\My Tool.exe"), new Pin(PinKind.App, "Vendor.App_abc!Main")]),
            new() { [@"C:\Tools\My Tool.exe"] = new ResolvedPin(null, null), ["Vendor.App_abc!Main"] = new ResolvedPin(" \t ", null) });

        Assert.AreEqual("My Tool", items.Items[0].Name);
        Assert.AreEqual("Vendor.App_abc!Main", items.Items[1].Name);
    }

    [TestMethod]
    public void DropsAnIconThatIsntAPlainFileName()
    {
        var items = Build(
            new HarborSettings([new Pin(PinKind.App, "A")]),
            new() { ["A"] = new ResolvedPin("A", @"..\0123456789abcdef.bgra") });

        Assert.IsNull(items.Items[0].Icon);
    }

    // Whatever Windows returns, the name fits dockItems.
    [TestMethod]
    public void CleansNames()
    {
        Assert.AreEqual("a b", DockItemsBuilder.CleanName("a\tb"));
        Assert.AreEqual("a\uFFFDb", DockItemsBuilder.CleanName("a\ud800b"));
        Assert.AreEqual("\U0001D538 x", DockItemsBuilder.CleanName("  \U0001D538 x\n"));
        Assert.IsNull(DockItemsBuilder.CleanName("\r\n"));
        Assert.IsNull(DockItemsBuilder.CleanName(null));

        var cut = DockItemsBuilder.CleanName(string.Concat(Enumerable.Repeat("\U0001D538", 200)))!;
        Assert.IsTrue(MessageCodec.IsValidDockItemText(cut, DockItems.MaxNameLength));
        Assert.AreEqual(DockItems.MaxNameLength * 2, cut.Length, "cut at 128 characters, not 128 UTF-16 units");
    }

    [TestMethod]
    public void EveryBuiltListEncodes()
    {
        var pins = Enumerable.Range(0, HarborSettings.MaxPinned).Select(i => new Pin(PinKind.App, $"App{i}")).ToList();
        var answers = pins.ToDictionary(pin => pin.Target, pin => (object?)new ResolvedPin(new string('\u0001', 500) + pin.Target, null));

        var items = Build(new HarborSettings(pins), answers);

        Assert.HasCount(HarborSettings.MaxPinned, items.Items);
        Assert.IsTrue(MessageCodec.TryDecode(MessageCodec.Encode(items), out var decoded, out var error), $"{error}");
        Assert.AreEqual(items, decoded);
    }
}
