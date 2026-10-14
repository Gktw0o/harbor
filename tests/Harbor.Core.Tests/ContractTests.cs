using System.Reflection;
using System.Text;
using System.Text.Json.Nodes;
using Harbor.Core.Ipc;
using Harbor.Core.Taskbar;

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

    // State files: contracts/state/<name>.schema.json with fixtures in contracts/state/fixtures/<name>/.
    // Each name needs a decoder here; a new schema without one fails the test.
    private static byte[]? DecodeAndReencodeState(string name, byte[] text) => name switch
    {
        "taskbar-restore" => RestoreRecord.TryDecode(text, out var record) ? RestoreRecord.Encode(record) : null,
        _ => throw new AssertFailedException($"no decoder for state file {name}"),
    };

    [TestMethod]
    public void StateFixturesRoundTripOrAreRejected()
    {
        var stateDir = Path.Combine(ContractsDir, "state");
        var schemas = Directory.GetFiles(stateDir, "*.schema.json");
        Assert.AreNotEqual(0, schemas.Length, stateDir);

        foreach (var schema in schemas)
        {
            var name = Path.GetFileName(schema)[..^".schema.json".Length];
            var fixtures = Path.Combine(stateDir, "fixtures", name);

            var valid = Directory.GetFiles(Path.Combine(fixtures, "valid"), "*.json");
            Assert.AreNotEqual(0, valid.Length, $"{name}: no valid fixtures");
            foreach (var fixture in valid)
            {
                var text = ReadLine(fixture);
                var reencoded = DecodeAndReencodeState(name, text);
                Assert.IsNotNull(reencoded, $"{name}/{Path.GetFileName(fixture)}: rejected");
                Assert.IsTrue(JsonNode.DeepEquals(JsonNode.Parse(reencoded), JsonNode.Parse(text)),
                    $"{name}/{Path.GetFileName(fixture)}: re-encoded as {Encoding.UTF8.GetString(reencoded)}");
            }

            var invalid = Directory.GetFiles(Path.Combine(fixtures, "invalid"), "*.json");
            Assert.AreNotEqual(0, invalid.Length, $"{name}: no invalid fixtures");
            foreach (var fixture in invalid)
            {
                Assert.IsNull(DecodeAndReencodeState(name, ReadLine(fixture)), $"{name}/{Path.GetFileName(fixture)}: accepted");
            }
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

    [TestMethod]
    public void EncodedSetTaskbarMatchesFixture() =>
        Assert.IsTrue(JsonNode.DeepEquals(
            JsonNode.Parse(MessageCodec.Encode(new SetTaskbar(Hidden: true))),
            JsonNode.Parse(ReadLine(Path.Combine(ContractsDir, "fixtures", "valid", "set-taskbar-hidden.json")))));

    [TestMethod]
    public void EncodedTaskbarStateMatchesFixture() =>
        Assert.IsTrue(JsonNode.DeepEquals(
            JsonNode.Parse(MessageCodec.Encode(new TaskbarState(Hidden: false, TaskbarBlock.PanicHotkey))),
            JsonNode.Parse(ReadLine(Path.Combine(ContractsDir, "fixtures", "valid", "taskbar-state-blocked-panic-hotkey.json")))));

    // Every TaskbarBlock has a wire name, and a valid fixture uses it: an enum value added on one side
    // without the other fails here or in the fixtures.
    [TestMethod]
    public void EveryTaskbarBlockHasAWireNameAndAFixture()
    {
        var fixtures = string.Join("\n", ListFixtures("valid").Select(File.ReadAllText));
        foreach (var block in Enum.GetValues<TaskbarBlock>())
        {
            var name = MessageCodec.WireName(block);
            Assert.IsTrue(MessageCodec.TryParseWireName(name, out var parsed) && parsed == block, $"{block}: {name}");
            StringAssert.Contains(fixtures, $"\"blocked\":\"{name}\"", $"no valid fixture uses {name}");
        }
    }
}
