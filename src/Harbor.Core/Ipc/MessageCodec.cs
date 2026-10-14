using System.Buffers;
using System.Diagnostics.CodeAnalysis;
using System.Text.Json;

namespace Harbor.Core.Ipc;

public enum DecodeError
{
    MalformedJson,
    NotAnObject,
    MissingField,
    UnexpectedField,
    InvalidField,
    UnknownType,
    UnsupportedVersion,
}

/// <summary>
/// Wire codec for control-channel messages: one compact UTF-8 JSON object per line. Must stay in step
/// with the dock's codec (src/dock/core/ipc/codec.cpp); both run the fixtures in contracts/fixtures.
/// </summary>
public static class MessageCodec
{
    private const string PingType = "ping";
    private const string PongType = "pong";
    private const ulong PingVersion = 1;
    private const ulong PongVersion = 1;

    /// <summary>Encodes a message as one compact JSON line, without the trailing '\n'.</summary>
    public static byte[] Encode(Message message)
    {
        ArgumentNullException.ThrowIfNull(message);
        var (type, version, seq) = message switch
        {
            Ping ping => (PingType, PingVersion, ping.Seq),
            Pong pong => (PongType, PongVersion, pong.Seq),
            _ => throw new ArgumentException($"Unknown message type {message.GetType().Name}.", nameof(message)),
        };

        var buffer = new ArrayBufferWriter<byte>();
        using (var writer = new Utf8JsonWriter(buffer))
        {
            writer.WriteStartObject();
            writer.WriteNumber("v", version);
            writer.WriteString("type", type);
            writer.WriteStartObject("payload");
            writer.WriteNumber("seq", seq);
            writer.WriteEndObject();
            writer.WriteEndObject();
        }
        return buffer.WrittenSpan.ToArray();
    }

    /// <summary>
    /// Decodes one line (without its '\n'). Strict: anything that doesn't match contracts/*.schema.json,
    /// including duplicate keys, is rejected with an error. Never throws on bad input.
    /// </summary>
    public static bool TryDecode(ReadOnlyMemory<byte> line, [NotNullWhen(true)] out Message? message, out DecodeError error)
    {
        message = null;
        JsonDocument document;
        try
        {
            document = JsonDocument.Parse(line);
        }
        catch (JsonException)
        {
            error = DecodeError.MalformedJson;
            return false;
        }

        using (document)
        {
            var failure = DecodeDocument(document.RootElement, out message);
            error = failure.GetValueOrDefault();
            return failure is null;
        }
    }

    // Returns null on success.
    private static DecodeError? DecodeDocument(JsonElement root, out Message? message)
    {
        message = null;
        if (root.ValueKind != JsonValueKind.Object)
        {
            return DecodeError.NotAnObject;
        }

        // Count every property, duplicates included: JsonDocument keeps all copies of a repeated key.
        JsonElement? version = null, type = null, payload = null;
        var count = 0;
        foreach (var property in root.EnumerateObject())
        {
            count++;
            switch (property.Name)
            {
                case "v": version = property.Value; break;
                case "type": type = property.Value; break;
                case "payload": payload = property.Value; break;
            }
        }

        if (version is null || type is null || payload is null)
        {
            return DecodeError.MissingField;
        }
        if (count != 3)
        {
            return DecodeError.UnexpectedField;
        }
        // TryGetUInt64 throws for non-numbers, so check the kind first.
        if (version.Value.ValueKind != JsonValueKind.Number || !version.Value.TryGetUInt64(out var versionNumber) ||
            type.Value.ValueKind != JsonValueKind.String)
        {
            return DecodeError.InvalidField;
        }

        if (type.Value.ValueEquals(PingType))
        {
            return DecodeSeqMessage(versionNumber, PingVersion, payload.Value, seq => new Ping(seq), out message);
        }
        if (type.Value.ValueEquals(PongType))
        {
            return DecodeSeqMessage(versionNumber, PongVersion, payload.Value, seq => new Pong(seq), out message);
        }
        return DecodeError.UnknownType;
    }

    // Payload of both ping and pong: exactly {"seq": <uint32>}. Returns null on success.
    private static DecodeError? DecodeSeqMessage(
        ulong version, ulong expectedVersion, JsonElement payload, Func<uint, Message> create, out Message? message)
    {
        message = null;
        if (version != expectedVersion)
        {
            return DecodeError.UnsupportedVersion;
        }
        if (payload.ValueKind != JsonValueKind.Object)
        {
            return DecodeError.InvalidField;
        }

        JsonElement? seq = null;
        var count = 0;
        foreach (var property in payload.EnumerateObject())
        {
            count++;
            if (property.NameEquals("seq"))
            {
                seq = property.Value;
            }
        }

        if (seq is null)
        {
            return DecodeError.MissingField;
        }
        if (count != 1)
        {
            return DecodeError.UnexpectedField;
        }
        // Fails for negatives, fractions, exponents and anything above uint.MaxValue, like the dock's decoder.
        if (seq.Value.ValueKind != JsonValueKind.Number || !seq.Value.TryGetUInt32(out var seqNumber))
        {
            return DecodeError.InvalidField;
        }

        message = create(seqNumber);
        return null;
    }
}
