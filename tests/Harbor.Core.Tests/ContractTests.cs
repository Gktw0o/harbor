using System.Reflection;
using System.Text;
using System.Text.Json.Nodes;
using Harbor.Core.Ipc;

namespace Harbor.Core.Tests;

// Contract tests: the C# codec against the shared fixtures in contracts/fixtures. tests/dock runs the same
// fixtures against the JSON Schemas and the C++ codec, which together proves schema, C++ and C# agree.
// Fixtures are listed at run time, so a new file takes effect without rebuilding.
[TestClass]
public sealed class ContractTests
{
    private static readonly string ContractsDir = typeof(ContractTests).Assembly
        .GetCustomAttributes<AssemblyMetadataAttribute>()
        .Single(attribute => attribute.Key == "ContractsDir").Value!;

    private static string[] ListFixtures(string kind) =>
        [.. Directory.EnumerateFiles(Path.Combine(ContractsDir, "fixtures", kind), "*.json").Order(StringComparer.Ordinal)];

    // One line, as it would arrive from the pipe: trailing line endings are not part of the message.
    private static byte[] ReadLine(string path) =>
        Encoding.UTF8.GetBytes(File.ReadAllText(path).TrimEnd('\r', '\n'));

    [TestMethod]
    public void ValidFixturesDecodeAndReencodeToEqualJson()
    {
        var fixtures = ListFixtures("valid");
        Assert.AreNotEqual(0, fixtures.Length, ContractsDir);

        foreach (var fixture in fixtures)
        {
            var name = Path.GetFileName(fixture);
            var line = ReadLine(fixture);
            Assert.IsTrue(MessageCodec.TryDecode(line, out var message, out var error), $"{name}: rejected ({error})");

            var reencoded = MessageCodec.Encode(message);
            Assert.IsTrue(JsonNode.DeepEquals(JsonNode.Parse(reencoded), JsonNode.Parse(line)),
                $"{name}: re-encoded as {Encoding.UTF8.GetString(reencoded)}");
            Assert.AreEqual(-1, Array.IndexOf(reencoded, (byte)'\n'), $"{name}: encoded message would break line framing");
        }
    }

    [TestMethod]
    public void InvalidFixturesAreRejected()
    {
        var fixtures = ListFixtures("invalid");
        Assert.AreNotEqual(0, fixtures.Length, ContractsDir);

        foreach (var fixture in fixtures)
        {
            var line = ReadLine(fixture);
            Assert.IsFalse(MessageCodec.TryDecode(line, out _, out _),
                $"{Path.GetFileName(fixture)}: accepted {Encoding.UTF8.GetString(line)}");
        }
    }

    [TestMethod]
    public void EncodedPingMatchesFixture() =>
        Assert.IsTrue(JsonNode.DeepEquals(
            JsonNode.Parse(MessageCodec.Encode(new Ping(42))),
            JsonNode.Parse(ReadLine(Path.Combine(ContractsDir, "fixtures", "valid", "ping.json")))));

    [TestMethod]
    public void EncodedPongMatchesFixture() =>
        Assert.IsTrue(JsonNode.DeepEquals(
            JsonNode.Parse(MessageCodec.Encode(new Pong(42))),
            JsonNode.Parse(ReadLine(Path.Combine(ContractsDir, "fixtures", "valid", "pong.json")))));
}
