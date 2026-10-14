using System.Buffers;
using System.Diagnostics.CodeAnalysis;
using System.Text.Json;

namespace Harbor.Core.Taskbar;

/// <summary>
/// Saved by the dock just before it turns on the taskbar's auto-hide, so the user's original setting can be
/// put back after a crash or a reboot. Stored as <c>%LOCALAPPDATA%\Harbor\taskbar-restore.json</c>; the
/// schema is contracts/state/taskbar-restore.schema.json, shared with the dock's
/// core/taskbar/restore_record.cpp.
/// </summary>
/// <param name="StateBefore"><c>SHAppBarMessage(ABM_GETSTATE)</c> before Harbor changed it, 0-3.</param>
/// <param name="CreatedUtc">When the record was written, e.g. <c>2026-10-14T15:30:00Z</c>.</param>
public sealed record RestoreRecord(uint StateBefore, string CreatedUtc)
{
    public const string FileName = "taskbar-restore.json";
    private const ulong Version = 1;
    private const uint MaxState = 3;

    public static string PathIn(string dataDir) => Path.Combine(dataDir, FileName);

    public static byte[] Encode(RestoreRecord record)
    {
        ArgumentNullException.ThrowIfNull(record);
        var buffer = new ArrayBufferWriter<byte>();
        using (var writer = new Utf8JsonWriter(buffer))
        {
            writer.WriteStartObject();
            writer.WriteNumber("v", Version);
            writer.WriteNumber("stateBefore", record.StateBefore);
            writer.WriteString("createdUtc", record.CreatedUtc);
            writer.WriteEndObject();
        }
        return buffer.WrittenSpan.ToArray();
    }

    /// <summary>Strict, like the IPC codec: anything off-schema, including duplicate keys, fails. Never throws.</summary>
    public static bool TryDecode(ReadOnlyMemory<byte> text, [NotNullWhen(true)] out RestoreRecord? record)
    {
        record = null;
        JsonDocument document;
        try
        {
            document = JsonDocument.Parse(text);
        }
        catch (JsonException)
        {
            return false;
        }

        using (document)
        {
            var root = document.RootElement;
            if (root.ValueKind != JsonValueKind.Object)
            {
                return false;
            }
            // Count every property, duplicates included: JsonDocument keeps all copies of a repeated key.
            JsonElement? version = null, state = null, created = null;
            var count = 0;
            foreach (var property in root.EnumerateObject())
            {
                count++;
                switch (property.Name)
                {
                    case "v": version = property.Value; break;
                    case "stateBefore": state = property.Value; break;
                    case "createdUtc": created = property.Value; break;
                }
            }
            // TryGetUInt64/32 throw for non-numbers, so check the kind first.
            if (count != 3 || version is not { ValueKind: JsonValueKind.Number } || !version.Value.TryGetUInt64(out var versionNumber) ||
                versionNumber != Version || state is not { ValueKind: JsonValueKind.Number } ||
                !state.Value.TryGetUInt32(out var stateNumber) || stateNumber > MaxState ||
                created is not { ValueKind: JsonValueKind.String } || created.Value.GetString() is not { } createdText ||
                !IsUtcTimestamp(createdText))
            {
                return false;
            }
            record = new RestoreRecord(stateNumber, createdText);
            return true;
        }
    }

    /// <summary><c>YYYY-MM-DDTHH:MM:SSZ</c> with digits in every numeric position.</summary>
    public static bool IsUtcTimestamp(string text)
    {
        ArgumentNullException.ThrowIfNull(text);
        const string Shape = "dddd-dd-ddTdd:dd:ddZ";
        if (text.Length != Shape.Length)
        {
            return false;
        }
        for (var i = 0; i < text.Length; i++)
        {
            var ok = Shape[i] == 'd' ? char.IsAsciiDigit(text[i]) : text[i] == Shape[i];
            if (!ok)
            {
                return false;
            }
        }
        return true;
    }
}
