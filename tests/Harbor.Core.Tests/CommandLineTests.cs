using System.Runtime.InteropServices;
using Harbor.Core.Supervision;

namespace Harbor.Core.Tests;

// CommandLine must produce what CommandLineToArgvW (the dock's parser) splits back exactly.
[TestClass]
public sealed partial class CommandLineTests
{
    [TestMethod]
    [DataRow("plain")]
    [DataRow("C:\\Users\\some one\\AppData\\Local\\Temp\\harbor-test-1")]
    [DataRow("")]
    [DataRow("say \"hi\"")]
    [DataRow("trailing\\")]
    [DataRow("trailing space\\")]
    [DataRow("back\\\\\"quote")]
    [DataRow("Ünïcødé ğ 日本")]
    public void ArgumentsSurviveTheRoundTrip(string argument)
    {
        var split = Split(CommandLine.Join(["harbor-dock.exe", argument, "--next"]));
        CollectionAssert.AreEqual(new[] { "harbor-dock.exe", argument, "--next" }, split);
    }

    private static string[] Split(string commandLine)
    {
        var argv = CommandLineToArgv(commandLine, out var count);
        try
        {
            return [.. Enumerable.Range(0, count).Select(i => Marshal.PtrToStringUni(Marshal.ReadIntPtr(argv, i * nint.Size))!)];
        }
        finally
        {
            LocalFree(argv);
        }
    }

    [LibraryImport("shell32.dll", EntryPoint = "CommandLineToArgvW", StringMarshalling = StringMarshalling.Utf16)]
    private static partial nint CommandLineToArgv(string commandLine, out int count);

    [LibraryImport("kernel32.dll")]
    private static partial nint LocalFree(nint memory);
}
