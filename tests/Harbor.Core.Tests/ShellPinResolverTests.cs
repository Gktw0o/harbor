using Harbor.Core.Apps;
using Harbor.Core.Settings;

namespace Harbor.Core.Tests;

// The real shell. File Explorer is on every Windows 11, CI runners included.
[TestClass]
public sealed class ShellPinResolverTests
{
    [TestMethod]
    public void FindsAnInstalledApp()
    {
        var resolved = new ShellPinResolver().Resolve(new Pin(PinKind.App, "Microsoft.Windows.Explorer"));

        Assert.IsNotNull(resolved);
        Assert.IsFalse(string.IsNullOrWhiteSpace(resolved.Name));
    }

    [TestMethod]
    public void DoesntFindAnAppThatIsntInstalled() =>
        Assert.IsNull(new ShellPinResolver().Resolve(new Pin(PinKind.App, $"Harbor.Test.NotInstalled.{Guid.NewGuid():N}")));

    [TestMethod]
    public void FindsAFileByItsPath()
    {
        using var dir = new TempDirectory();
        var path = Path.Combine(dir.Path, "Ünïcødé tool.exe");
        File.WriteAllBytes(path, []);

        var resolved = new ShellPinResolver().Resolve(new Pin(PinKind.Path, path));

        // "Ünïcødé tool" or "Ünïcødé tool.exe", depending on whether Explorer shows extensions.
        StringAssert.StartsWith(resolved?.Name, "Ünïcødé tool");
    }

    [TestMethod]
    public void DoesntFindAMissingFile()
    {
        using var dir = new TempDirectory();
        Assert.IsNull(new ShellPinResolver().Resolve(new Pin(PinKind.Path, Path.Combine(dir.Path, "missing.exe"))));
    }
}
