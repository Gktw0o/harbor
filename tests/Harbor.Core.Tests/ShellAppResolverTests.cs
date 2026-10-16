using Harbor.Core.Apps;

namespace Harbor.Core.Tests;

// The real shell: File Explorer and cmd.exe are on every Windows 11, CI runners included.
[TestClass]
public sealed partial class ShellAppResolverTests
{
    private static readonly string Cmd = Path.Combine(Environment.SystemDirectory, "cmd.exe");

    [TestMethod]
    public void FindsAnAppInStartWithItsIcon()
    {
        using var dataDir = new TempDirectory();
        var resolved = new ShellAppResolver(new IconStore(dataDir.Path, new CapturingLog())).ResolveAumid("Microsoft.Windows.Explorer");

        Assert.IsNotNull(resolved);
        Assert.IsFalse(string.IsNullOrWhiteSpace(resolved.Name));
        Assert.IsNotNull(resolved.Icon);
    }

    [TestMethod]
    public void DoesntFindAnAppStartDoesntKnow() =>
        Assert.IsNull(new ShellAppResolver().ResolveAumid($"Harbor.Test.NotInstalled.{Guid.NewGuid():N}"));

    // The description people know it by, not "cmd".
    [TestMethod]
    public void NamesAnExecutableByItsDescription()
    {
        var resolved = new ShellAppResolver().ResolveExe(Cmd);

        Assert.IsNotNull(resolved);
        Assert.IsFalse(string.IsNullOrWhiteSpace(resolved.Name));
        Assert.AreNotEqual("cmd", resolved.Name);
    }

    [TestMethod]
    public void AnExecutableWithoutADescriptionGoesByItsFileName()
    {
        using var dir = new TempDirectory();
        var path = Path.Combine(dir.Path, "Ünïcødé tool.exe");
        File.WriteAllBytes(path, []);

        Assert.AreEqual("Ünïcødé tool", new ShellAppResolver().ResolveExe(path)?.Name);
        Assert.IsNull(new ShellAppResolver().ResolveExe(Path.Combine(dir.Path, "missing.exe")));
    }

    [TestMethod]
    public void ReadsAShortcutsTargetAndAppUserModelId()
    {
        using var dir = new TempDirectory();
        var link = Path.Combine(dir.Path, "Ünïcødé tool.lnk");
        Shortcuts.Create(link, Cmd, "Harbor.Test.Shortcut");

        var shortcut = new ShellAppResolver().ReadShortcut(link);

        Assert.IsNotNull(shortcut);
        Assert.AreEqual(Cmd, shortcut.Target, ignoreCase: true);
        Assert.AreEqual("Harbor.Test.Shortcut", shortcut.Aumid);
        Assert.IsNull(new ShellAppResolver().ReadShortcut(Path.Combine(dir.Path, "missing.lnk")));
    }

    [TestMethod]
    public void AWindowWithoutARelaunchNameHasNone() =>
        // The desktop window carries no app properties.
        Assert.IsNull(new ShellAppResolver().WindowRelaunchName((uint)GetDesktopWindow()));

    [System.Runtime.InteropServices.LibraryImport("user32.dll")]
    private static partial nint GetDesktopWindow();
}
