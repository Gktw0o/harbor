using System.Text;
using Harbor.Core.Ipc;

namespace Harbor.Core.Tests;

[TestClass]
public sealed class MessageCodecTests
{
    private static DecodeError DecodeErrorOf(string line)
    {
        Assert.IsFalse(MessageCodec.TryDecode(Encoding.UTF8.GetBytes(line), out _, out var error), $"accepted: {line}");
        return error;
    }

    [TestMethod]
    public void EncodesPingInDocumentedFieldOrder() =>
        Assert.AreEqual("""{"v":1,"type":"ping","payload":{"seq":42}}""", Encoding.UTF8.GetString(MessageCodec.Encode(new Ping(42))));

    [TestMethod]
    public void EncodesPong() =>
        Assert.AreEqual("""{"v":1,"type":"pong","payload":{"seq":7}}""", Encoding.UTF8.GetString(MessageCodec.Encode(new Pong(7))));

    [TestMethod]
    public void EncodesTheTaskbarMessages()
    {
        Assert.AreEqual("""{"v":1,"type":"setTaskbar","payload":{"hidden":true}}""",
            Encoding.UTF8.GetString(MessageCodec.Encode(new SetTaskbar(Hidden: true))));
        Assert.AreEqual("""{"v":1,"type":"taskbarState","payload":{"hidden":false,"blocked":"hotkeyUnavailable"}}""",
            Encoding.UTF8.GetString(MessageCodec.Encode(new TaskbarState(Hidden: false, TaskbarBlock.HotkeyUnavailable))));
    }

    [TestMethod]
    public void RoundTripsTheTaskbarMessages()
    {
        foreach (Message message in new Message[]
        {
            new SetTaskbar(true), new SetTaskbar(false), new TaskbarState(true, TaskbarBlock.None),
            new TaskbarState(false, TaskbarBlock.KillOnCloseJob),
        })
        {
            Assert.IsTrue(MessageCodec.TryDecode(MessageCodec.Encode(message), out var decoded, out _));
            Assert.AreEqual(message, decoded);
        }
    }

    // Same as tests/dock/codec_tests.cpp: every value has a wire name that maps back.
    [TestMethod]
    public void EveryTaskbarBlockHasAWireName()
    {
        foreach (var block in Enum.GetValues<TaskbarBlock>())
        {
            Assert.IsTrue(MessageCodec.TryParseWireName(MessageCodec.WireName(block), out var parsed));
            Assert.AreEqual(block, parsed);
        }
        Assert.IsFalse(MessageCodec.TryParseWireName("PanicHotkey", out _));
    }

    [TestMethod]
    public void ReportsSpecificErrorsForTheTaskbarMessages()
    {
        Assert.AreEqual(DecodeError.MissingField, DecodeErrorOf("""{"v":1,"type":"setTaskbar","payload":{}}"""));
        Assert.AreEqual(DecodeError.InvalidField, DecodeErrorOf("""{"v":1,"type":"setTaskbar","payload":{"hidden":1}}"""));
        Assert.AreEqual(DecodeError.UnexpectedField, DecodeErrorOf("""{"v":1,"type":"setTaskbar","payload":{"hidden":true,"x":1}}"""));
        Assert.AreEqual(DecodeError.UnsupportedVersion, DecodeErrorOf("""{"v":2,"type":"setTaskbar","payload":{"hidden":true}}"""));
        Assert.AreEqual(DecodeError.InvalidField, DecodeErrorOf("""{"v":1,"type":"taskbarState","payload":{"hidden":false,"blocked":"x"}}"""));
        Assert.AreEqual(DecodeError.MissingField, DecodeErrorOf("""{"v":1,"type":"taskbarState","payload":{"hidden":false}}"""));
    }

    // "𝔸" (U+1D538): one character, four bytes in UTF-8, two UTF-16 code units.
    private const string Wide = "\U0001D538";

    private static string Repeat(string text, int count) => string.Concat(Enumerable.Repeat(text, count));

    private static string DockItemsLine(string kind, string target, string name, string icon = "null") =>
        $$$"""{"v":1,"type":"dockItems","payload":{"items":[{"kind":"{{{kind}}}","target":"{{{target}}}","name":"{{{name}}}","icon":{{{icon}}}}]}}""";

    private static bool Decodes(string line) => MessageCodec.TryDecode(Encoding.UTF8.GetBytes(line), out _, out _);

    // Same as Codec.EncodesDockItemsAsUtf8WithoutEscapes in tests/dock/codec_tests.cpp.
    [TestMethod]
    public void EncodesDockItemsAsUtf8WithoutEscapes()
    {
        var message = new DockItems(
        [
            new DockItem(DockItemKind.App, "Microsoft.Windows.Explorer", "File Explorer", "0123456789abcdef.bgra"),
            new DockItem(DockItemKind.Path, @"C:\Tools\ğ.exe", "\"ğ\" " + Wide, null),
        ]);
        Assert.AreEqual(
            """{"v":1,"type":"dockItems","payload":{"items":[""" +
            """{"kind":"app","target":"Microsoft.Windows.Explorer","name":"File Explorer","icon":"0123456789abcdef.bgra"},""" +
            """{"kind":"path","target":"C:\\Tools\\ğ.exe","name":"\"ğ\" """ + Wide + "\",\"icon\":null}]}}",
            Encoding.UTF8.GetString(MessageCodec.Encode(message)));
        Assert.AreEqual("""{"v":1,"type":"dockItems","payload":{"items":[]}}""", Encoding.UTF8.GetString(MessageCodec.Encode(new DockItems([]))));
    }

    [TestMethod]
    public void RoundTripsDockItems()
    {
        foreach (var message in new DockItems[]
        {
            new([]),
            new([new DockItem(DockItemKind.Path, @"C:\a b\c.lnk", "c", null)]),
            new(
            [
                new DockItem(DockItemKind.App, "MSEdge", "Microsoft Edge", "fedcba9876543210.bgra"),
                new DockItem(DockItemKind.App, Repeat(Wide, DockItems.MaxTargetLength), Repeat(Wide, DockItems.MaxNameLength), null),
            ]),
        })
        {
            Assert.IsTrue(MessageCodec.TryDecode(MessageCodec.Encode(message), out var decoded, out var error), $"{error}");
            Assert.AreEqual(message, decoded);
        }
    }

    // Lengths count characters, not bytes or UTF-16 code units, like JSON Schema's maxLength (and the dock).
    [TestMethod]
    public void BoundsDockItemTextInCharacters()
    {
        Assert.IsTrue(Decodes(DockItemsLine("app", Repeat("x", 260), "x")));
        Assert.IsFalse(Decodes(DockItemsLine("app", Repeat("x", 261), "x")));
        Assert.IsTrue(Decodes(DockItemsLine("app", Repeat(Wide, 260), Repeat(Wide, 128))));
        Assert.IsFalse(Decodes(DockItemsLine("app", "x", Repeat(Wide, 129))));
        Assert.IsFalse(Decodes(DockItemsLine("app", "", "x")));
        Assert.IsFalse(Decodes(DockItemsLine("app", "x", "")));
        // Only C0 control characters are refused: DEL and C1 controls cost at most two bytes.
        Assert.IsFalse(Decodes(DockItemsLine("app", "x", @"a\u001fb")));
        Assert.IsTrue(Decodes(DockItemsLine("app", "x", @"a\u007f\u0085b")));
    }

    [TestMethod]
    public void ChecksDockItemTextBeforeSending()
    {
        Assert.IsTrue(MessageCodec.IsValidDockItemText(Repeat(Wide, 3), 3));
        Assert.IsFalse(MessageCodec.IsValidDockItemText(Repeat(Wide, 4), 3));
        Assert.IsFalse(MessageCodec.IsValidDockItemText("", 3));
        Assert.IsFalse(MessageCodec.IsValidDockItemText("a\tb", 3));
        Assert.IsFalse(MessageCodec.IsValidDockItemText("a\ud800b", 3), "a lone surrogate can't be written as UTF-8");
        Assert.IsFalse(MessageCodec.IsValidDockItemText("\udc00", 3));
    }

    [TestMethod]
    public void AcceptsOnlyPlainIconFileNames()
    {
        Assert.IsTrue(Decodes(DockItemsLine("app", "x", "x", "\"0123456789abcdef.bgra\"")));
        Assert.IsTrue(Decodes(DockItemsLine("app", "x", "x", "null")));
        foreach (var icon in new[]
        {
            "\"0123456789abcdeg.bgra\"", "\"0123456789abcdef.bgraX\"", "\"0123456789abcdef0.bgra\"",
            "\"a\\\\0123456789abcdef.bgra\"", "\"\"", "false",
        })
        {
            Assert.IsFalse(Decodes(DockItemsLine("app", "x", "x", icon)), icon);
        }
    }

    [TestMethod]
    public void ReportsSpecificErrorsForDockItems()
    {
        Assert.AreEqual(DecodeError.MissingField, DecodeErrorOf("""{"v":1,"type":"dockItems","payload":{}}"""));
        Assert.AreEqual(DecodeError.MissingField, DecodeErrorOf("""{"v":1,"type":"dockItems","payload":{"items":[{"kind":"app"}]}}"""));
        Assert.AreEqual(DecodeError.UnexpectedField, DecodeErrorOf(
            """{"v":1,"type":"dockItems","payload":{"items":[{"kind":"app","target":"x","name":"x","icon":null,"x":1}]}}"""));
        Assert.AreEqual(DecodeError.InvalidField, DecodeErrorOf("""{"v":1,"type":"dockItems","payload":{"items":[1]}}"""));
        Assert.AreEqual(DecodeError.InvalidField, DecodeErrorOf(DockItemsLine("App", "x", "x")));
        Assert.AreEqual(DecodeError.UnsupportedVersion, DecodeErrorOf("""{"v":2,"type":"dockItems","payload":{"items":[]}}"""));

        var item = """{"kind":"app","target":"x","name":"x","icon":null}""";
        var tooMany = """{"v":1,"type":"dockItems","payload":{"items":[""" +
                      string.Join(",", Enumerable.Repeat(item, DockItems.MaxItems + 1)) + "]}}";
        Assert.AreEqual(DecodeError.InvalidField, DecodeErrorOf(tooMany));
    }

    [TestMethod]
    public void RoundTripsBothMessages()
    {
        foreach (Message message in new Message[] { new Ping(0), new Ping(uint.MaxValue), new Pong(123) })
        {
            Assert.IsTrue(MessageCodec.TryDecode(MessageCodec.Encode(message), out var decoded, out _));
            Assert.AreEqual(message, decoded);
        }
    }

    [TestMethod]
    public void AcceptsFieldsInAnyOrderAndWhitespace()
    {
        var line = Encoding.UTF8.GetBytes("""  { "payload" : { "seq" : 5 }, "type" : "pong", "v" : 1 } """);
        Assert.IsTrue(MessageCodec.TryDecode(line, out var decoded, out _));
        Assert.AreEqual(new Pong(5), decoded);
    }

    // Same cases as Codec.ReportsSpecificErrors in tests/dock/codec_tests.cpp.
    [TestMethod]
    public void ReportsSpecificErrors()
    {
        Assert.AreEqual(DecodeError.MalformedJson, DecodeErrorOf(""));
        Assert.AreEqual(DecodeError.MalformedJson, DecodeErrorOf("{"));
        Assert.AreEqual(DecodeError.NotAnObject, DecodeErrorOf("\"ping\""));
        Assert.AreEqual(DecodeError.MissingField, DecodeErrorOf("""{"v":1,"type":"ping"}"""));
        Assert.AreEqual(DecodeError.UnexpectedField, DecodeErrorOf("""{"v":1,"type":"ping","payload":{"seq":1},"x":0}"""));
        Assert.AreEqual(DecodeError.UnexpectedField, DecodeErrorOf("""{"v":1,"type":"ping","payload":{"seq":1,"x":0}}"""));
        Assert.AreEqual(DecodeError.InvalidField, DecodeErrorOf("""{"v":-1,"type":"ping","payload":{"seq":1}}"""));
        Assert.AreEqual(DecodeError.InvalidField, DecodeErrorOf("""{"v":1,"type":"ping","payload":{"seq":-1}}"""));
        Assert.AreEqual(DecodeError.UnknownType, DecodeErrorOf("""{"v":1,"type":"nope","payload":{"seq":1}}"""));
        Assert.AreEqual(DecodeError.UnsupportedVersion, DecodeErrorOf("""{"v":2,"type":"pong","payload":{"seq":1}}"""));
    }

    // JSON Schema can't express "no duplicate keys", so this lives in unit tests on both sides
    // (see Codec.RejectsDuplicateKeys in tests/dock) instead of contract fixtures.
    [TestMethod]
    public void RejectsDuplicateKeys()
    {
        Assert.AreEqual(DecodeError.UnexpectedField, DecodeErrorOf("""{"v":1,"v":1,"type":"ping","payload":{"seq":1}}"""));
        Assert.AreEqual(DecodeError.UnexpectedField, DecodeErrorOf("""{"v":1,"type":"ping","payload":{"seq":1,"seq":2}}"""));
        Assert.AreEqual(DecodeError.MissingField, DecodeErrorOf("""{"v":1,"type":"ping","payload":{"v":1}}"""));
        Assert.AreEqual(DecodeError.UnexpectedField, DecodeErrorOf(
            """{"v":1,"type":"dockItems","payload":{"items":[{"kind":"app","target":"x","name":"x","name":"y","icon":null}]}}"""));
    }

    [TestMethod]
    public void RejectsInvalidUtf8()
    {
        byte[] line = [.. "{\"v\":1,\"type\":\"p"u8, 0xFF, .. "\",\"payload\":{\"seq\":1}}"u8];
        Assert.IsFalse(MessageCodec.TryDecode(line, out _, out _));
        byte[] inAKey = [.. "{\"v\":1,\"type\":\"setTaskbar\",\"payload\":{\"hid"u8, 0xFF, .. "\":true}}"u8];
        Assert.IsFalse(MessageCodec.TryDecode(inAKey, out _, out _));
        byte[] inAnEnum = [.. "{\"v\":1,\"type\":\"taskbarState\",\"payload\":{\"hidden\":true,\"blocked\":\"n"u8, 0xFF, .. "\"}}"u8];
        Assert.IsFalse(MessageCodec.TryDecode(inAnEnum, out _, out _));
        byte[] inAName = [.. "{\"v\":1,\"type\":\"dockItems\",\"payload\":{\"items\":[{\"kind\":\"app\",\"target\":\"x\",\"name\":\"n"u8, 0xFF,
            .. "\",\"icon\":null}]}}"u8];
        Assert.IsFalse(MessageCodec.TryDecode(inAName, out _, out _));
    }

    [TestMethod]
    public void RejectsDeeplyNestedInputWithoutThrowing()
    {
        var nested = new string('[', 30000) + new string(']', 30000);
        Assert.AreEqual(DecodeError.MalformedJson, DecodeErrorOf(nested));
    }
}
