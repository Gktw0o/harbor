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
    private const ulong Version = 1;

    private static readonly (TaskbarBlock Value, string Name)[] BlockNames =
    [
        (TaskbarBlock.None, "none"),
        (TaskbarBlock.HotkeyUnavailable, "hotkeyUnavailable"),
        (TaskbarBlock.PanicHotkey, "panicHotkey"),
        (TaskbarBlock.ExplorerFighting, "explorerFighting"),
        (TaskbarBlock.TaskbarNotFound, "taskbarNotFound"),
        (TaskbarBlock.KillOnCloseJob, "killOnCloseJob"),
    ];

    /// <summary>Encodes a message as one compact JSON line, without the trailing '\n'.</summary>
    public static byte[] Encode(Message message)
    {
        ArgumentNullException.ThrowIfNull(message);
        var buffer = new ArrayBufferWriter<byte>();
        using (var writer = new Utf8JsonWriter(buffer))
        {
            writer.WriteStartObject();
            // Every message is at version 1 today; a message that moves on gets its own constant.
            writer.WriteNumber("v", Version);
            writer.WriteString("type", TypeName(message));
            writer.WriteStartObject("payload");
            switch (message)
            {
                case Ping ping:
                    writer.WriteNumber("seq", ping.Seq);
                    break;
                case Pong pong:
                    writer.WriteNumber("seq", pong.Seq);
                    break;
                case SetTaskbar setTaskbar:
                    writer.WriteBoolean("hidden", setTaskbar.Hidden);
                    break;
                case TaskbarState state:
                    writer.WriteBoolean("hidden", state.Hidden);
                    writer.WriteString("blocked", WireName(state.Blocked));
                    break;
            }
            writer.WriteEndObject();
            writer.WriteEndObject();
        }
        return buffer.WrittenSpan.ToArray();
    }

    /// <summary>
    /// Decodes one line (without its '\n'). Strict: anything that doesn't match contracts/*.schema.json,
    /// including duplicate keys, is rejected with an error. Never throws.
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
            DecodeError? failure;
            try
            {
                failure = DecodeDocument(document.RootElement, out message);
            }
            catch (InvalidOperationException)
            {
                // System.Text.Json checks UTF-8 only when text is read; the dock's parser rejects it up front.
                message = null;
                failure = DecodeError.MalformedJson;
            }
            error = failure.GetValueOrDefault();
            return failure is null;
        }
    }

    public static string WireName(TaskbarBlock block) =>
        Array.Find(BlockNames, entry => entry.Value == block).Name ?? "none";

    public static bool TryParseWireName(string name, out TaskbarBlock block)
    {
        foreach (var (value, wireName) in BlockNames)
        {
            if (wireName == name)
            {
                block = value;
                return true;
            }
        }
        block = TaskbarBlock.None;
        return false;
    }

    private static string TypeName(Message message) => message switch
    {
        Ping => "ping",
        Pong => "pong",
        SetTaskbar => "setTaskbar",
        TaskbarState => "taskbarState",
        _ => throw new ArgumentException($"Unknown message type {message.GetType().Name}.", nameof(message)),
    };

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

        Func<Dictionary<string, JsonElement>, (Message?, DecodeError?)>? decodePayload = type.Value.GetString() switch
        {
            "ping" => fields => DecodeSeq(fields, seq => new Ping(seq)),
            "pong" => fields => DecodeSeq(fields, seq => new Pong(seq)),
            "setTaskbar" => DecodeSetTaskbar,
            "taskbarState" => DecodeTaskbarState,
            _ => null,
        };
        if (decodePayload is null)
        {
            return DecodeError.UnknownType;
        }
        if (versionNumber != Version)
        {
            return DecodeError.UnsupportedVersion;
        }
        if (payload.Value.ValueKind != JsonValueKind.Object)
        {
            return DecodeError.InvalidField;
        }

        var fields = new Dictionary<string, JsonElement>(StringComparer.Ordinal);
        foreach (var property in payload.Value.EnumerateObject())
        {
            if (!fields.TryAdd(property.Name, property.Value))
            {
                return DecodeError.UnexpectedField;
            }
        }
        (message, var failure) = decodePayload(fields);
        return failure;
    }

    // The payload must have exactly these keys.
    private static DecodeError? CheckKeys(Dictionary<string, JsonElement> fields, params string[] keys)
    {
        if (!keys.All(fields.ContainsKey))
        {
            return DecodeError.MissingField;
        }
        return fields.Count != keys.Length ? DecodeError.UnexpectedField : null;
    }

    // Payload of both ping and pong: exactly {"seq": <uint32>}.
    private static (Message?, DecodeError?) DecodeSeq(Dictionary<string, JsonElement> fields, Func<uint, Message> create)
    {
        if (CheckKeys(fields, "seq") is { } error)
        {
            return (null, error);
        }
        // Fails for negatives, fractions, exponents and anything above uint.MaxValue, like the dock's decoder.
        var seq = fields["seq"];
        return seq.ValueKind == JsonValueKind.Number && seq.TryGetUInt32(out var value)
            ? (create(value), null)
            : (null, DecodeError.InvalidField);
    }

    private static (Message?, DecodeError?) DecodeSetTaskbar(Dictionary<string, JsonElement> fields)
    {
        if (CheckKeys(fields, "hidden") is { } error)
        {
            return (null, error);
        }
        return IsBoolean(fields["hidden"], out var hidden) ? (new SetTaskbar(hidden), null) : (null, DecodeError.InvalidField);
    }

    private static (Message?, DecodeError?) DecodeTaskbarState(Dictionary<string, JsonElement> fields)
    {
        if (CheckKeys(fields, "hidden", "blocked") is { } error)
        {
            return (null, error);
        }
        var blocked = fields["blocked"];
        return IsBoolean(fields["hidden"], out var hidden) && blocked.ValueKind == JsonValueKind.String &&
               TryParseWireName(blocked.GetString()!, out var block)
            ? (new TaskbarState(hidden, block), null)
            : (null, DecodeError.InvalidField);
    }

    private static bool IsBoolean(JsonElement element, out bool value)
    {
        value = element.ValueKind == JsonValueKind.True;
        return element.ValueKind is JsonValueKind.True or JsonValueKind.False;
    }
}
