using System.Reflection;
using System.Text;
using System.Text.Json.Nodes;
using Harbor.Core.Settings;

namespace Harbor.Core.Tests;

[TestClass]
public sealed class HarborSettingsTests
{
    private static readonly string ContractsDir = typeof(HarborSettingsTests).Assembly
        .GetCustomAttributes<AssemblyMetadataAttribute>()
        .Single(attribute => attribute.Key == "ContractsDir").Value!;

    private static string? ProblemWith(string text)
    {
        Assert.IsFalse(HarborSettings.TryDecode(Encoding.UTF8.GetBytes(text), out _, out var problem), $"accepted: {text}");
        return problem;
    }

    [TestMethod]
    public void DefaultsMatchTheContractFixture() =>
        Assert.IsTrue(JsonNode.DeepEquals(
            JsonNode.Parse(HarborSettings.Encode(HarborSettings.Defaults)),
            JsonNode.Parse(File.ReadAllText(Path.Combine(ContractsDir, "state", "fixtures", "settings", "valid", "defaults.json")))));

    [TestMethod]
    public void EncodesForPeopleAndRoundTrips()
    {
        var settings = new HarborSettings([new Pin(PinKind.Path, @"C:\Ünïcødé\app.exe"), new Pin(PinKind.App, "MSEdge")]);
        var text = HarborSettings.Encode(settings);
        // Indented, and text as UTF-8 rather than \u escapes, so the file reads well in an editor.
        StringAssert.Contains(Encoding.UTF8.GetString(text), "\"path\": \"C:\\\\Ünïcødé\\\\app.exe\"");
        Assert.IsTrue(HarborSettings.TryDecode(text, out var decoded, out var problem), problem);
        Assert.AreEqual(settings, decoded);
    }

    [TestMethod]
    public void AcceptsAByteOrderMark()
    {
        byte[] text = [0xEF, 0xBB, 0xBF, .. """{"v":1,"pinned":[{"app":"MSEdge"}]}"""u8];
        Assert.IsTrue(HarborSettings.TryDecode(text, out var settings, out var problem), problem);
        Assert.AreEqual(new HarborSettings([new Pin(PinKind.App, "MSEdge")]), settings);
    }

    // The file is edited by hand, so each problem names what's wrong and where.
    [TestMethod]
    public void SaysWhatIsWrong()
    {
        StringAssert.Contains(ProblemWith("""{"v":1,"pinned":["""), "isn't valid JSON");
        StringAssert.Contains(ProblemWith("""{"v":2,"pinned":[]}"""), "\"v\" must be 1");
        StringAssert.Contains(ProblemWith("""{"v":1,"pinned":[],"theme":"dark"}"""), "\"theme\" isn't a setting");
        StringAssert.Contains(ProblemWith("""{"v":1}"""), "\"pinned\" must be a list");
        StringAssert.Contains(ProblemWith("""{"v":1,"pinned":[{"app":"MSEdge"},{"exe":"x"}]}"""), "pinned[1] must be");
        StringAssert.Contains(ProblemWith("""{"v":1,"pinned":[{"path":"tools\\x.exe"}]}"""), "pinned[0]: \"tools\\x.exe\" isn't an absolute path");
        StringAssert.Contains(ProblemWith("""{"v":1,"pinned":[{"app":""}]}"""), "pinned[0] must be 1 to 260 characters");
        var tooMany = """{"v":1,"pinned":[""" + string.Join(",", Enumerable.Repeat("""{"app":"x"}""", 41)) + "]}";
        StringAssert.Contains(ProblemWith(tooMany), "has 41 apps; the dock holds 40");
    }

    // JSON Schema can't express "no duplicate keys" (see MessageCodecTests.RejectsDuplicateKeys).
    [TestMethod]
    public void RejectsDuplicateKeys()
    {
        StringAssert.Contains(ProblemWith("""{"v":1,"v":1,"pinned":[]}"""), "has \"v\" twice");
        StringAssert.Contains(ProblemWith("""{"v":1,"pinned":[{"app":"a","app":"b"}]}"""), "pinned[0] has \"app\" twice");
    }

    [TestMethod]
    public void RejectsInvalidUtf8()
    {
        byte[] text = [.. """{"v":1,"pinned":[{"app":"a"""u8, 0xFF, .. "\"}]}"u8];
        Assert.AreEqual("it isn't valid UTF-8", HarborSettings.TryDecode(text, out _, out var problem) ? null : problem);
    }

    [TestMethod]
    public void KnowsAbsolutePaths()
    {
        Assert.IsTrue(HarborSettings.IsAbsolutePath(@"C:\x.exe"));
        Assert.IsTrue(HarborSettings.IsAbsolutePath(@"\\server\share\x.lnk"));
        Assert.IsTrue(HarborSettings.IsAbsolutePath(@"\\?\C:\x.exe"));
        Assert.IsFalse(HarborSettings.IsAbsolutePath("C:x.exe"));
        Assert.IsFalse(HarborSettings.IsAbsolutePath(@"\x.exe"));
        Assert.IsFalse(HarborSettings.IsAbsolutePath("C:/x.exe"));
        Assert.IsFalse(HarborSettings.IsAbsolutePath("x.exe"));
    }
}
