using System.Reflection;
using Harbor.Core.Apps;
using Harbor.Core.Settings;

namespace Harbor.Core.Tests;

[TestClass]
public sealed class IconFileTests
{
    private static readonly string Fixtures = Path.Combine(typeof(IconFileTests).Assembly
        .GetCustomAttributes<AssemblyMetadataAttribute>()
        .Single(attribute => attribute.Key == "ContractsDir").Value!, "icons", "fixtures", "valid");

    // tests/dock reads the same fixtures with the dock's parser.
    [TestMethod]
    public void WritesTheContractFixturesByteForByte()
    {
        var square = new IconImage(2, 2, [0, 0, 255, 255, 0, 128, 0, 128, 0, 0, 0, 0, 255, 255, 255, 255]);
        CollectionAssert.AreEqual(File.ReadAllBytes(Path.Combine(Fixtures, "square-2x2.bgra")), IconFile.Encode(square));

        var row = new IconImage(3, 1, [255, 0, 0, 255, 64, 64, 64, 64, 0, 0, 0, 0]);
        CollectionAssert.AreEqual(File.ReadAllBytes(Path.Combine(Fixtures, "row-3x1.bgra")), IconFile.Encode(row));

        var largest = new IconImage(256, 256, new byte[256 * 256 * 4]);
        CollectionAssert.AreEqual(File.ReadAllBytes(Path.Combine(Fixtures, "largest-256x256.bgra")), IconFile.Encode(largest));
    }

    [TestMethod]
    public void RefusesWhatTheFormatCantHold()
    {
        Assert.ThrowsExactly<ArgumentException>(() => IconFile.Encode(new IconImage(0, 1, [])));
        Assert.ThrowsExactly<ArgumentException>(() => IconFile.Encode(new IconImage(257, 1, new byte[257 * 4])));
        Assert.ThrowsExactly<ArgumentException>(() => IconFile.Encode(new IconImage(1, 1, new byte[3])));
    }

    [TestMethod]
    public void NamesFilesByTheirContent()
    {
        var a = IconFile.Encode(new IconImage(1, 1, [1, 2, 3, 255]));
        var b = IconFile.Encode(new IconImage(1, 1, [1, 2, 4, 255]));

        StringAssert.Matches(IconFile.NameOf(a), new System.Text.RegularExpressions.Regex("^[0-9a-f]{16}\\.bgra$"));
        Assert.AreEqual(IconFile.NameOf(a), IconFile.NameOf([.. a]));
        Assert.AreNotEqual(IconFile.NameOf(a), IconFile.NameOf(b));
    }

    [TestMethod]
    public void NormalizesWhatWindowsReturns()
    {
        // No alpha at all: an opaque bitmap.
        byte[] opaque = [10, 20, 30, 0, 40, 50, 60, 0];
        IconFile.Normalize(opaque);
        CollectionAssert.AreEqual(new byte[] { 10, 20, 30, 255, 40, 50, 60, 255 }, opaque);

        // With alpha, colors are clamped to it, so the dock's premultiplied check passes.
        byte[] straight = [200, 100, 50, 128, 0, 0, 0, 0];
        IconFile.Normalize(straight);
        CollectionAssert.AreEqual(new byte[] { 128, 100, 50, 128, 0, 0, 0, 0 }, straight);
    }
}

[TestClass]
public sealed class IconStoreTests
{
    private static IconImage Pixel(byte blue) => new(1, 1, [blue, 0, 0, 255]);

    [TestMethod]
    public void SavesEachIconOnceUnderItsContentName()
    {
        using var dataDir = new TempDirectory();
        var store = new IconStore(dataDir.Path, new CapturingLog());

        var name = store.Save(Pixel(1));
        Assert.IsNotNull(name);
        Assert.AreEqual(name, store.Save(Pixel(1)));
        CollectionAssert.AreEqual(IconFile.Encode(Pixel(1)), File.ReadAllBytes(Path.Combine(dataDir.Path, "icons", name)));
        Assert.HasCount(1, Directory.GetFiles(store.Directory), "a temporary file was left behind");
    }

    // The dock may still draw the previous list until it gets the new one.
    [TestMethod]
    public void DeletesIconsNeitherTheNewListNorThePreviousOneUses()
    {
        using var dataDir = new TempDirectory();
        var store = new IconStore(dataDir.Path, new CapturingLog());
        // The first list is published.
        var first = store.Save(Pixel(1))!;
        store.KeepOnly([first]);
        // The next one is built (an icon saved but left out, say) and published.
        var second = store.Save(Pixel(2))!;
        var unused = store.Save(Pixel(3))!;
        File.WriteAllBytes(Path.Combine(store.Directory, "other-writer.tmp"), []);
        store.KeepOnly([second]);

        Assert.IsTrue(File.Exists(Path.Combine(store.Directory, first)), "the previous list's icon went too soon");
        Assert.IsTrue(File.Exists(Path.Combine(store.Directory, second)));
        Assert.IsFalse(File.Exists(Path.Combine(store.Directory, unused)));
        Assert.IsTrue(File.Exists(Path.Combine(store.Directory, "other-writer.tmp")));

        // One list later, the first one's icon goes.
        store.KeepOnly([second]);
        Assert.IsFalse(File.Exists(Path.Combine(store.Directory, first)));
    }

    // A running app's icon (named in appInfo) stays while the dock that heard of it is connected, whatever the
    // pinned list does (docs/adr/0010).
    [TestMethod]
    public void IconsHeldForRunningAppsSurvivePinChangesUntilReleased()
    {
        using var dataDir = new TempDirectory();
        var store = new IconStore(dataDir.Path, new CapturingLog());
        var running = store.Save(Pixel(4))!;
        store.Hold(running);
        store.KeepOnly([]);
        store.KeepOnly([]);
        Assert.IsTrue(File.Exists(Path.Combine(store.Directory, running)));

        store.ReleaseHeld();
        store.KeepOnly([]);
        Assert.IsFalse(File.Exists(Path.Combine(store.Directory, running)));
    }

    // The real shell: File Explorer's icon, in the dock's format.
    [TestMethod]
    public void TheShellResolverSavesAnInstalledAppsIcon()
    {
        using var dataDir = new TempDirectory();
        var store = new IconStore(dataDir.Path, new CapturingLog());

        var resolved = new ShellPinResolver(store).Resolve(new Pin(PinKind.App, "Microsoft.Windows.Explorer"));

        Assert.IsNotNull(resolved?.Icon);
        var file = File.ReadAllBytes(Path.Combine(store.Directory, resolved.Icon));
        CollectionAssert.AreEqual("HIC1"u8.ToArray(), file[..4]);
        var width = BitConverter.ToInt32(file, 4);
        var height = BitConverter.ToInt32(file, 8);
        Assert.IsTrue(width is > 0 and <= 256 && height is > 0 and <= 256, $"{width}x{height}");
        Assert.AreEqual(12 + (width * height * 4), file.Length);
        Assert.AreEqual(IconFile.NameOf(file), resolved.Icon);
        // Something is drawn: not every pixel is transparent.
        Assert.IsTrue(file.Skip(12).Where((_, i) => i % 4 == 3).Any(alpha => alpha != 0));
    }
}
