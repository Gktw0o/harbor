using System.Buffers;
using System.Diagnostics.CodeAnalysis;
using System.Text.Json;
using Harbor.Core.Ipc;

namespace Harbor.Core.Settings;

public enum PinKind
{
    /// <summary>An AppUserModelID, as in <c>shell:AppsFolder</c>.</summary>
    App,

    /// <summary>An absolute path (drive or UNC) to an .exe or .lnk.</summary>
    Path,
}

/// <summary>A pinned app.</summary>
public sealed record Pin(PinKind Kind, string Target);

/// <summary>
/// Harbor's settings, <c>%LOCALAPPDATA%\Harbor\settings.json</c> (contracts/state/settings.schema.json). Harbor
/// Shell writes the defaults on first run; people may edit the file by hand, so decoding says what's wrong.
/// Compares by content.
/// </summary>
public sealed record HarborSettings(IReadOnlyList<Pin> Pinned)
{
    public const string FileName = "settings.json";

    /// <summary>As many as dockItems carries.</summary>
    public const int MaxPinned = DockItems.MaxItems;

    /// <summary>Characters (runes) in a pin's target.</summary>
    public const int MaxTargetLength = DockItems.MaxTargetLength;

    private const ulong Version = 1;

    /// <summary>File Explorer, Microsoft Edge, Windows Terminal and Settings. Apps that aren't installed are skipped when the dock's list is made.</summary>
    public static HarborSettings Defaults { get; } = new(
    [
        new Pin(PinKind.App, "Microsoft.Windows.Explorer"),
        new Pin(PinKind.App, "MSEdge"),
        new Pin(PinKind.App, "Microsoft.WindowsTerminal_8wekyb3d8bbwe!App"),
        new Pin(PinKind.App, "windows.immersivecontrolpanel_cw5n1h2txyewy!microsoft.windows.immersivecontrolpanel"),
    ]);

    public static string PathIn(string dataDir) => System.IO.Path.Combine(dataDir, FileName);

    public bool Equals(HarborSettings? other) => other is not null && Pinned.SequenceEqual(other.Pinned);

    public override int GetHashCode() => Pinned.Aggregate(0, (hash, pin) => HashCode.Combine(hash, pin));

    /// <summary>Indented, for people who edit the file.</summary>
    public static byte[] Encode(HarborSettings settings)
    {
        ArgumentNullException.ThrowIfNull(settings);
        var buffer = new ArrayBufferWriter<byte>();
        using (var writer = new Utf8JsonWriter(buffer, new JsonWriterOptions { Indented = true, Encoder = MinimalJsonEncoder.Instance }))
        {
            writer.WriteStartObject();
            writer.WriteNumber("v", Version);
            writer.WriteStartArray("pinned");
            foreach (var pin in settings.Pinned)
            {
                writer.WriteStartObject();
                writer.WriteString(pin.Kind == PinKind.Path ? "path" : "app", pin.Target);
                writer.WriteEndObject();
            }
            writer.WriteEndArray();
            writer.WriteEndObject();
        }
        return buffer.WrittenSpan.ToArray();
    }

    /// <summary>
    /// Strict, like the IPC codec: anything off-schema, including duplicate keys, fails, with a reason for
    /// people in <paramref name="problem"/>. A UTF-8 byte order mark is allowed. Never throws.
    /// </summary>
    public static bool TryDecode(ReadOnlyMemory<byte> text, [NotNullWhen(true)] out HarborSettings? settings, [NotNullWhen(false)] out string? problem)
    {
        settings = null;
        if (text.Span.StartsWith("\uFEFF"u8))
        {
            text = text[3..];
        }
        JsonDocument document;
        try
        {
            document = JsonDocument.Parse(text);
        }
        catch (JsonException exception)
        {
            problem = $"it isn't valid JSON ({exception.Message})";
            return false;
        }

        using (document)
        {
            try
            {
                problem = Decode(document.RootElement, out settings);
            }
            catch (InvalidOperationException)
            {
                // System.Text.Json checks UTF-8 only when text is read.
                problem = "it isn't valid UTF-8";
            }
            return problem is null;
        }
    }

    // Returns null on success, else the problem.
    private static string? Decode(JsonElement root, out HarborSettings? settings)
    {
        settings = null;
        if (ReadFields(root, "the file", out var fields) is { } fieldsProblem)
        {
            return fieldsProblem;
        }
        if (fields.Keys.FirstOrDefault(key => key is not ("v" or "pinned")) is { } unknown)
        {
            return $"\"{unknown}\" isn't a setting Harbor knows";
        }
        if (!fields.TryGetValue("v", out var version) || version.ValueKind != JsonValueKind.Number ||
            !version.TryGetUInt64(out var versionNumber) || versionNumber != Version)
        {
            return $"\"v\" must be {Version}";
        }
        if (!fields.TryGetValue("pinned", out var pinned) || pinned.ValueKind != JsonValueKind.Array)
        {
            return "\"pinned\" must be a list";
        }
        if (pinned.GetArrayLength() > MaxPinned)
        {
            return $"\"pinned\" has {pinned.GetArrayLength()} apps; the dock holds {MaxPinned}";
        }

        var pins = new List<Pin>(pinned.GetArrayLength());
        foreach (var element in pinned.EnumerateArray())
        {
            var where = $"pinned[{pins.Count}]";
            if (ReadFields(element, where, out var pin) is { } pinProblem)
            {
                return pinProblem;
            }
            if (pin.Count != 1 || pin.Keys.Single() is not ("app" or "path") ||
                pin.Values.Single() is not { ValueKind: JsonValueKind.String } value)
            {
                return $"{where} must be {{\"app\": \"<AppUserModelID>\"}} or {{\"path\": \"<absolute path>\"}}";
            }
            var kind = pin.ContainsKey("path") ? PinKind.Path : PinKind.App;
            var target = value.GetString()!;
            if (!MessageCodec.IsValidDockItemText(target, MaxTargetLength))
            {
                return $"{where} must be 1 to {MaxTargetLength} characters, without control characters";
            }
            if (kind == PinKind.Path && !IsAbsolutePath(target))
            {
                return $"{where}: \"{target}\" isn't an absolute path (like C:\\Tools\\app.exe)";
            }
            pins.Add(new Pin(kind, target));
        }
        settings = new HarborSettings(pins);
        return null;
    }

    /// <summary>A drive path (<c>C:\…</c>) or a UNC path (<c>\\server\…</c>), as the schema requires.</summary>
    public static bool IsAbsolutePath(string path)
    {
        ArgumentNullException.ThrowIfNull(path);
        return (path.Length >= 3 && char.IsAsciiLetter(path[0]) && path[1] == ':' && path[2] == '\\') || path.StartsWith(@"\\", StringComparison.Ordinal);
    }

    // An object's properties by name. Returns the problem if it isn't an object or repeats a key.
    private static string? ReadFields(JsonElement element, string where, out Dictionary<string, JsonElement> fields)
    {
        fields = new Dictionary<string, JsonElement>(StringComparer.Ordinal);
        if (element.ValueKind != JsonValueKind.Object)
        {
            return $"{where} must be a JSON object";
        }
        foreach (var property in element.EnumerateObject())
        {
            if (!fields.TryAdd(property.Name, property.Value))
            {
                return $"{where} has \"{property.Name}\" twice";
            }
        }
        return null;
    }
}
