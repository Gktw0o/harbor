using System.Text;
using Harbor.Core.Logging;

namespace Harbor.Core.Tests;

// Same cases as tests/dock/file_log_tests.cpp.
[TestClass]
public sealed class FileLogTests
{
    // "2026-10-14T15:30:00.123Z [info] " is 32 bytes, so a 50-character message makes an 83-byte line.
    private static string Message(char fill) => new(fill, 50);

    private static string Read(string path) => File.Exists(path) ? File.ReadAllText(path, Encoding.UTF8) : "";

    [TestMethod]
    public void WritesLinesToTheNamedFile()
    {
        using var dir = new TempDirectory();
        var log = new FileLog(dir.Path, "shell");
        log.Info("first");
        log.Warn("second");

        var contents = Read(Path.Combine(dir.Path, "shell.log"));
        StringAssert.Contains(contents, "Z [info] first\n");
        StringAssert.Contains(contents, "Z [warn] second\n");
        Assert.IsLessThan(contents.IndexOf("second", StringComparison.Ordinal), contents.IndexOf("first", StringComparison.Ordinal));
    }

    [TestMethod]
    public void AppendsToAnExistingFile()
    {
        using var dir = new TempDirectory();
        new FileLog(dir.Path, "shell").Info("from the first run");
        new FileLog(dir.Path, "shell").Info("from the second run");

        var contents = Read(Path.Combine(dir.Path, "shell.log"));
        StringAssert.Contains(contents, "from the first run");
        StringAssert.Contains(contents, "from the second run");
    }

    [TestMethod]
    public void RollsOverToTheOldFileWhenFull()
    {
        using var dir = new TempDirectory();
        var log = new FileLog(dir.Path, "shell", maxBytes: 200);
        // Two 83-byte lines fit in 200 bytes; the third rolls over, and so does the fifth.
        foreach (var fill in "abcde")
        {
            log.Info(Message(fill));
        }

        var old = Read(Path.Combine(dir.Path, "shell.old.log"));
        var current = Read(Path.Combine(dir.Path, "shell.log"));
        Assert.AreEqual(2 * 83, old.Length);
        StringAssert.Contains(old, Message('c'));
        StringAssert.Contains(old, Message('d'));
        Assert.AreEqual(83, current.Length);
        StringAssert.Contains(current, Message('e'));
    }

    [TestMethod]
    public void CreatesMissingDirectories()
    {
        using var dir = new TempDirectory();
        var nested = Path.Combine(dir.Path, "a", "b");
        new FileLog(nested, "shell").Info("hello");
        StringAssert.Contains(Read(Path.Combine(nested, "shell.log")), "hello");
    }

    [TestMethod]
    public void WorksInANonAsciiDirectory()
    {
        using var dir = new TempDirectory(" Ünïcødé ğ 日本");
        new FileLog(dir.Path, "shell").Info("ü");
        StringAssert.Contains(Read(Path.Combine(dir.Path, "shell.log")), "[info] ü\n");
    }

    [TestMethod]
    public void WithoutADirectoryLogsToTheDebuggerOnly() => new FileLog(null, "shell").Error("nowhere to go");
}
