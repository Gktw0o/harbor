using System.Buffers;
using System.Diagnostics.CodeAnalysis;
using System.Text;
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

    private static readonly SearchValues<char> LowercaseHexDigits = SearchValues.Create("0123456789abcdef");

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
        using (var writer = new Utf8JsonWriter(buffer, new JsonWriterOptions { Encoder = MinimalJsonEncoder.Instance }))
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
                case DockItems dockItems:
                    WriteDockItems(writer, dockItems);
                    break;
                case AppLookup lookup:
                    writer.WriteNumber("id", lookup.Id);
                    WriteOptionalString(writer, "aumid", lookup.Aumid);
                    WriteOptionalString(writer, "exe", lookup.Exe);
                    if (lookup.Window is { } window)
                    {
                        writer.WriteNumber("window", window);
                    }
                    else
                    {
                        writer.WriteNull("window");
                    }
                    break;
                case AppInfo info:
                    writer.WriteNumber("id", info.Id);
                    writer.WriteString("name", info.Name);
                    WriteOptionalString(writer, "icon", info.Icon);
                    WriteTarget(writer, "pinned", info.Pinned);
                    WriteTarget(writer, "launch", info.Launch);
                    break;
                case PinRequest request:
                    writer.WriteString("action", request.Action == PinAction.Pin ? "pin" : "unpin");
                    writer.WriteString("kind", WireName(request.Item.Kind));
                    writer.WriteString("target", request.Item.Target);
                    break;
                case DockStateMessage state:
                    WriteRect(writer, "monitor", state.Monitor);
                    WriteRect(writer, "work", state.Work);
                    writer.WriteNumber("dpi", state.Dpi);
                    writer.WriteBoolean("fullScreen", state.FullScreen);
                    writer.WriteBoolean("visible", state.Visible);
                    break;
                case IslandState island:
                    writer.WriteBoolean("ready", island.Ready);
                    writer.WriteBoolean("open", island.Open);
                    if (island.Hotkey is { } hotkey)
                    {
                        writer.WriteNumber("hotkey", hotkey);
                    }
                    else
                    {
                        writer.WriteNull("hotkey");
                    }
                    break;
                case Launch launch:
                    writer.WriteString("kind", WireName(launch.Item.Kind));
                    writer.WriteString("target", launch.Item.Target);
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
        DockItems => "dockItems",
        AppLookup => "appLookup",
        AppInfo => "appInfo",
        PinRequest => "pinRequest",
        DockStateMessage => "dockState",
        IslandState => "islandState",
        Launch => "launch",
        _ => throw new ArgumentException($"Unknown message type {message.GetType().Name}.", nameof(message)),
    };

    private static void WriteRect(Utf8JsonWriter writer, string name, ScreenRect rect)
    {
        writer.WriteStartObject(name);
        writer.WriteNumber("left", rect.Left);
        writer.WriteNumber("top", rect.Top);
        writer.WriteNumber("right", rect.Right);
        writer.WriteNumber("bottom", rect.Bottom);
        writer.WriteEndObject();
    }

    private static string WireName(DockItemKind kind) => kind == DockItemKind.Path ? "path" : "app";

    private static void WriteOptionalString(Utf8JsonWriter writer, string name, string? value)
    {
        if (value is null)
        {
            writer.WriteNull(name);
        }
        else
        {
            writer.WriteString(name, value);
        }
    }

    private static void WriteTarget(Utf8JsonWriter writer, string name, AppTarget? target)
    {
        if (target is null)
        {
            writer.WriteNull(name);
            return;
        }
        writer.WriteStartObject(name);
        writer.WriteString("kind", WireName(target.Kind));
        writer.WriteString("target", target.Target);
        writer.WriteEndObject();
    }

    private static void WriteDockItems(Utf8JsonWriter writer, DockItems message)
    {
        writer.WriteStartArray("items");
        foreach (var item in message.Items)
        {
            writer.WriteStartObject();
            writer.WriteString("kind", WireName(item.Kind));
            writer.WriteString("target", item.Target);
            writer.WriteString("name", item.Name);
            if (item.Icon is null)
            {
                writer.WriteNull("icon");
            }
            else
            {
                writer.WriteString("icon", item.Icon);
            }
            writer.WriteEndObject();
        }
        writer.WriteEndArray();
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

        Func<Dictionary<string, JsonElement>, (Message?, DecodeError?)>? decodePayload = type.Value.GetString() switch
        {
            "ping" => fields => DecodeSeq(fields, seq => new Ping(seq)),
            "pong" => fields => DecodeSeq(fields, seq => new Pong(seq)),
            "setTaskbar" => DecodeSetTaskbar,
            "taskbarState" => DecodeTaskbarState,
            "dockItems" => DecodeDockItems,
            "appLookup" => DecodeAppLookup,
            "appInfo" => DecodeAppInfo,
            "pinRequest" => DecodePinRequest,
            "dockState" => DecodeDockState,
            "islandState" => DecodeIslandState,
            "launch" => DecodeLaunch,
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
        if (ReadFields(payload.Value, out var fields) is { } fieldError)
        {
            return fieldError;
        }
        (message, var failure) = decodePayload(fields);
        return failure;
    }

    // An object's properties by name; a repeated name is an unexpected field.
    private static DecodeError? ReadFields(JsonElement element, out Dictionary<string, JsonElement> fields)
    {
        fields = new Dictionary<string, JsonElement>(StringComparer.Ordinal);
        if (element.ValueKind != JsonValueKind.Object)
        {
            return DecodeError.InvalidField;
        }
        foreach (var property in element.EnumerateObject())
        {
            if (!fields.TryAdd(property.Name, property.Value))
            {
                return DecodeError.UnexpectedField;
            }
        }
        return null;
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

    private static (Message?, DecodeError?) DecodeDockItems(Dictionary<string, JsonElement> fields)
    {
        if (CheckKeys(fields, "items") is { } error)
        {
            return (null, error);
        }
        var items = fields["items"];
        if (items.ValueKind != JsonValueKind.Array || items.GetArrayLength() > DockItems.MaxItems)
        {
            return (null, DecodeError.InvalidField);
        }

        var list = new List<DockItem>(items.GetArrayLength());
        foreach (var element in items.EnumerateArray())
        {
            if ((ReadFields(element, out var item) ?? CheckKeys(item, "kind", "target", "name", "icon")) is { } itemError)
            {
                return (null, itemError);
            }
            var kind = item["kind"].ValueKind == JsonValueKind.String ? item["kind"].GetString() : null;
            var icon = item["icon"];
            if (kind is not ("app" or "path") ||
                !TryGetText(item["target"], DockItems.MaxTargetLength, out var target) ||
                !TryGetText(item["name"], DockItems.MaxNameLength, out var name) ||
                !(icon.ValueKind == JsonValueKind.Null ||
                  (icon.ValueKind == JsonValueKind.String && IsIconFileName(icon.GetString()!))))
            {
                return (null, DecodeError.InvalidField);
            }
            list.Add(new DockItem(kind == "path" ? DockItemKind.Path : DockItemKind.App, target, name,
                icon.ValueKind == JsonValueKind.Null ? null : icon.GetString()));
        }
        return (new DockItems(list), null);
    }

    private static (Message?, DecodeError?) DecodeAppLookup(Dictionary<string, JsonElement> fields)
    {
        if (CheckKeys(fields, "id", "aumid", "exe", "window") is { } error)
        {
            return (null, error);
        }
        var window = fields["window"];
        uint handle = 0;
        if (!TryGetPositiveUInt32(fields["id"], out var id) ||
            !TryGetOptionalText(fields["aumid"], AppLookup.MaxAumidLength, out var aumid) ||
            !TryGetOptionalText(fields["exe"], AppLookup.MaxExeLength, out var exe) ||
            (window.ValueKind != JsonValueKind.Null && !TryGetPositiveUInt32(window, out handle)))
        {
            return (null, DecodeError.InvalidField);
        }
        return (new AppLookup(id, aumid, exe, window.ValueKind == JsonValueKind.Null ? null : handle), null);
    }

    private static (Message?, DecodeError?) DecodeAppInfo(Dictionary<string, JsonElement> fields)
    {
        if (CheckKeys(fields, "id", "name", "icon", "pinned", "launch") is { } error)
        {
            return (null, error);
        }
        var icon = fields["icon"];
        if (!TryGetPositiveUInt32(fields["id"], out var id) ||
            !TryGetText(fields["name"], DockItems.MaxNameLength, out var name) ||
            !(icon.ValueKind == JsonValueKind.Null ||
              (icon.ValueKind == JsonValueKind.String && IsIconFileName(icon.GetString()!))) ||
            !TryGetOptionalTarget(fields["pinned"], out var pinned) ||
            !TryGetOptionalTarget(fields["launch"], out var launch))
        {
            return (null, DecodeError.InvalidField);
        }
        return (new AppInfo(id, name, icon.ValueKind == JsonValueKind.Null ? null : icon.GetString(), pinned, launch), null);
    }

    private static (Message?, DecodeError?) DecodePinRequest(Dictionary<string, JsonElement> fields)
    {
        if (CheckKeys(fields, "action", "kind", "target") is { } error)
        {
            return (null, error);
        }
        var action = fields["action"].ValueKind == JsonValueKind.String ? fields["action"].GetString() : null;
        if (action is not ("pin" or "unpin") || !TryGetKind(fields["kind"], out var kind) ||
            !TryGetText(fields["target"], DockItems.MaxTargetLength, out var target))
        {
            return (null, DecodeError.InvalidField);
        }
        return (new PinRequest(action == "pin" ? PinAction.Pin : PinAction.Unpin, new AppTarget(kind, target)), null);
    }

    private static (Message?, DecodeError?) DecodeDockState(Dictionary<string, JsonElement> fields)
    {
        if (CheckKeys(fields, "monitor", "work", "dpi", "fullScreen", "visible") is { } error)
        {
            return (null, error);
        }
        var dpi = fields["dpi"];
        if (!TryGetRect(fields["monitor"], out var monitor) || !TryGetRect(fields["work"], out var work) ||
            dpi.ValueKind != JsonValueKind.Number || !dpi.TryGetInt32(out var dots) || dots < DockStateMessage.MinDpi ||
            dots > DockStateMessage.MaxDpi || !IsBoolean(fields["fullScreen"], out var fullScreen) ||
            !IsBoolean(fields["visible"], out var visible))
        {
            return (null, DecodeError.InvalidField);
        }
        return (new DockStateMessage(monitor, work, dots, fullScreen, visible), null);
    }

    private static (Message?, DecodeError?) DecodeIslandState(Dictionary<string, JsonElement> fields)
    {
        if (CheckKeys(fields, "ready", "open", "hotkey") is { } error)
        {
            return (null, error);
        }
        var hotkey = fields["hotkey"];
        int? key = null;
        if (hotkey.ValueKind != JsonValueKind.Null)
        {
            if (hotkey.ValueKind != JsonValueKind.Number || !hotkey.TryGetInt32(out var code) ||
                code < IslandState.MinHotkey || code > IslandState.MaxHotkey)
            {
                return (null, DecodeError.InvalidField);
            }
            key = code;
        }
        if (!IsBoolean(fields["ready"], out var ready) || !IsBoolean(fields["open"], out var open))
        {
            return (null, DecodeError.InvalidField);
        }
        return (new IslandState(ready, open, key), null);
    }

    private static (Message?, DecodeError?) DecodeLaunch(Dictionary<string, JsonElement> fields)
    {
        if (CheckKeys(fields, "kind", "target") is { } error)
        {
            return (null, error);
        }
        if (!TryGetKind(fields["kind"], out var kind) || !TryGetText(fields["target"], DockItems.MaxTargetLength, out var target))
        {
            return (null, DecodeError.InvalidField);
        }
        return (new Launch(new AppTarget(kind, target)), null);
    }

    // {"left", "top", "right", "bottom"}, each an integer within DockStateMessage.MaxCoordinate.
    private static bool TryGetRect(JsonElement element, [NotNullWhen(true)] out ScreenRect? rect)
    {
        rect = null;
        if ((ReadFields(element, out var fields) ?? CheckKeys(fields, "left", "top", "right", "bottom")) is not null)
        {
            return false;
        }
        var sides = new int[4];
        string[] names = ["left", "top", "right", "bottom"];
        for (var i = 0; i < names.Length; i++)
        {
            var side = fields[names[i]];
            if (side.ValueKind != JsonValueKind.Number || !side.TryGetInt32(out sides[i]) ||
                sides[i] < -DockStateMessage.MaxCoordinate || sides[i] > DockStateMessage.MaxCoordinate)
            {
                return false;
            }
        }
        rect = new ScreenRect(sides[0], sides[1], sides[2], sides[3]);
        return true;
    }

    // null, or {"kind", "target"} as in dockItems.
    private static bool TryGetOptionalTarget(JsonElement element, out AppTarget? target)
    {
        target = null;
        if (element.ValueKind == JsonValueKind.Null)
        {
            return true;
        }
        if ((ReadFields(element, out var fields) ?? CheckKeys(fields, "kind", "target")) is not null ||
            !TryGetKind(fields["kind"], out var kind) ||
            !TryGetText(fields["target"], DockItems.MaxTargetLength, out var text))
        {
            return false;
        }
        target = new AppTarget(kind, text);
        return true;
    }

    private static bool TryGetKind(JsonElement element, out DockItemKind kind)
    {
        var name = element.ValueKind == JsonValueKind.String ? element.GetString() : null;
        kind = name == "path" ? DockItemKind.Path : DockItemKind.App;
        return name is "app" or "path";
    }

    // An integer from 1 to 2^32 - 1 (ids and window handles).
    private static bool TryGetPositiveUInt32(JsonElement element, out uint value)
    {
        value = 0;
        return element.ValueKind == JsonValueKind.Number && element.TryGetUInt32(out value) && value != 0;
    }

    private static bool TryGetOptionalText(JsonElement element, int maxLength, out string? text)
    {
        text = null;
        if (element.ValueKind == JsonValueKind.Null)
        {
            return true;
        }
        var ok = TryGetText(element, maxLength, out var value);
        text = value;
        return ok;
    }

    /// <summary>
    /// Whether text fits a dockItems field: 1 to <paramref name="maxLength"/> characters (runes, like JSON
    /// Schema's maxLength), no control characters (U+0000-U+001F) and no lone surrogates.
    /// </summary>
    public static bool IsValidDockItemText(string text, int maxLength)
    {
        ArgumentNullException.ThrowIfNull(text);
        var rest = text.AsSpan();
        var length = 0;
        while (!rest.IsEmpty)
        {
            if (Rune.DecodeFromUtf16(rest, out var rune, out var used) != OperationStatus.Done || rune.Value < 0x20 ||
                ++length > maxLength)
            {
                return false;
            }
            rest = rest[used..];
        }
        return length > 0;
    }

    /// <summary>Whether a name is "&lt;16 lowercase hex digits&gt;.bgra": a plain file name in the icons folder.</summary>
    public static bool IsIconFileName(string name)
    {
        ArgumentNullException.ThrowIfNull(name);
        return name.Length == 21 && name.EndsWith(".bgra", StringComparison.Ordinal) &&
               name.AsSpan(0, 16).IndexOfAnyExcept(LowercaseHexDigits) < 0;
    }

    private static bool TryGetText(JsonElement element, int maxLength, [NotNullWhen(true)] out string? text)
    {
        text = element.ValueKind == JsonValueKind.String ? element.GetString() : null;
        return text is not null && IsValidDockItemText(text, maxLength);
    }

    private static bool IsBoolean(JsonElement element, out bool value)
    {
        value = element.ValueKind == JsonValueKind.True;
        return element.ValueKind is JsonValueKind.True or JsonValueKind.False;
    }
}
