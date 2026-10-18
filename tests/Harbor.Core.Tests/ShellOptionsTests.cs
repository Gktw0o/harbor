namespace Harbor.Core.Tests;

// Harbor.Shell.exe's command line: the dock's rules (tests/dock/command_line_tests.cpp), all or nothing.
[TestClass]
public sealed class ShellOptionsTests
{
    [TestMethod]
    public void NoArgumentsMeansTheRealShell()
    {
        var (options, error) = ShellOptions.Parse([]);
        Assert.IsNull(error);
        Assert.IsFalse(options!.TestMode);
        Assert.IsTrue(options.Taskbar.IsReal);
        Assert.AreEqual(HarborPaths.DefaultDataDir, options.DataDirOrDefault);
        Assert.IsEmpty(options.DockArguments);
    }

    [TestMethod]
    public void TheTestOptionsGoTogetherAndOnToTheDock()
    {
        var (options, error) = ShellOptions.Parse(["--pipe-name", "harbor.test.1", "--data-dir", @"C:\temp\x\..\h", "--test-taskbar", "ab-12"]);
        Assert.IsNull(error);
        Assert.IsTrue(options!.TestMode);
        Assert.IsFalse(options.Taskbar.IsReal);
        Assert.AreEqual(@"C:\temp\h", options.DataDirOrDefault);
        CollectionAssert.AreEqual(
            new[] { "--pipe-name", "harbor.test.1", "--data-dir", @"C:\temp\h", "--test-taskbar", "ab-12" },
            options.DockArguments.ToArray());
    }

    // Otherwise a test Shell could serve the real pipe, or restore the real taskbar from a test's record.
    [TestMethod]
    public void RejectsTestOptionsOnTheirOwn()
    {
        Assert.IsNotNull(ShellOptions.Parse(["--pipe-name", "harbor.test.1"]).Error);
        Assert.IsNotNull(ShellOptions.Parse(["--data-dir", @"C:\t", "--test-taskbar", "ab"]).Error);
        Assert.IsNotNull(ShellOptions.Parse(["--test-taskbar", "ab"]).Error);
    }

    // Where tests may take the focus, the test Shell's dock may too; never outside test mode.
    [TestMethod]
    public void TestForegroundGoesOnToTheDockInTestModeOnly()
    {
        var (options, error) = ShellOptions.Parse(["--test-foreground", "--pipe-name", "harbor.test.1", "--data-dir", @"C:\t", "--test-taskbar", "ab"]);
        Assert.IsNull(error);
        Assert.AreEqual("--test-foreground", options!.DockArguments[^1]);
        Assert.IsNotNull(ShellOptions.Parse(["--test-foreground"]).Error);
        Assert.IsNotNull(ShellOptions.Parse(["--test-foreground", "--test-foreground", "--pipe-name", "harbor.test.1", "--data-dir", @"C:\t", "--test-taskbar", "ab"]).Error);
    }

    // A refused command line still says where its log should go, so the user's own log is never written to.
    [TestMethod]
    public void ARefusedCommandLineStillNamesItsDataDir()
    {
        string[] arguments = ["--data-dir", @"C:\t\x\..", "--test-taskbar", "AB"];
        Assert.IsNotNull(ShellOptions.Parse(arguments).Error);
        Assert.AreEqual(@"C:\t", ShellOptions.DataDirNamedIn(arguments));
        Assert.IsNull(ShellOptions.DataDirNamedIn(["--data-dir", "relative"]));
        Assert.IsNull(ShellOptions.DataDirNamedIn([]));
    }

    [TestMethod]
    public void RejectsWhatTheDockWouldReject()
    {
        string[][] bad =
        [
            ["--pipe-name", "other.1", "--data-dir", @"C:\t", "--test-taskbar", "ab"],
            ["--pipe-name", "harbor.test.1", "--data-dir", @"relative\dir", "--test-taskbar", "ab"],
            ["--pipe-name", "harbor.test.1", "--data-dir", @"C:\t", "--test-taskbar", "AB"],
            ["--pipe-name", "harbor.test.1", "--data-dir", @"C:\t", "--test-taskbar", new string('a', 65)],
            ["--pipe-name", "harbor.test.1", "--pipe-name", "harbor.test.2", "--data-dir", @"C:\t", "--test-taskbar", "ab"],
            ["--pipe-name", "harbor.test.1", "--data-dir", @"C:\t", "--test-taskbar"],
            ["--verbose"],
        ];
        foreach (var arguments in bad)
        {
            Assert.IsNotNull(ShellOptions.Parse(arguments).Error, string.Join(' ', arguments));
        }
    }
}
