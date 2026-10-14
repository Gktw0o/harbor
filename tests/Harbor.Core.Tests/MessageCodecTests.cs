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
    }

    [TestMethod]
    public void RejectsInvalidUtf8()
    {
        byte[] line = [.. "{\"v\":1,\"type\":\"p"u8, 0xFF, .. "\",\"payload\":{\"seq\":1}}"u8];
        Assert.IsFalse(MessageCodec.TryDecode(line, out _, out _));
    }

    [TestMethod]
    public void RejectsDeeplyNestedInputWithoutThrowing()
    {
        var nested = new string('[', 30000) + new string(']', 30000);
        Assert.AreEqual(DecodeError.MalformedJson, DecodeErrorOf(nested));
    }
}
