using System.Text;
using System.Text.Encodings.Web;

namespace Harbor.Core.Ipc;

/// <summary>
/// Escapes only what JSON requires ('"', '\' and the control characters U+0000-U+001F) and leaves the rest
/// for the writer to put out as UTF-8, as the dock's encoder does. The built-in encoders escape every
/// character outside the Basic Multilingual Plane as two \u escapes, 12 bytes for one emoji, which would
/// break dockItems' size bound (contracts/dockItems.schema.json).
/// </summary>
internal sealed class MinimalJsonEncoder : JavaScriptEncoder
{
    private MinimalJsonEncoder()
    {
    }

    public static MinimalJsonEncoder Instance { get; } = new();

    // The longest escape, "\u001F".
    public override int MaxOutputCharactersPerInputCharacter => 6;

    public override bool WillEncode(int unicodeScalar) => unicodeScalar is < 0x20 or '"' or '\\';

    public override unsafe int FindFirstCharacterToEncode(char* text, int textLength)
    {
        for (var i = 0; i < textLength; i++)
        {
            if (WillEncode(text[i]))
            {
                return i;
            }
        }
        return -1;
    }

    public override unsafe bool TryEncodeUnicodeScalar(int unicodeScalar, char* buffer, int bufferLength, out int numberOfCharactersWritten)
    {
        var destination = new Span<char>(buffer, bufferLength);
        string? escaped = unicodeScalar switch
        {
            '"' => "\\\"",
            '\\' => "\\\\",
            '\b' => "\\b",
            '\f' => "\\f",
            '\n' => "\\n",
            '\r' => "\\r",
            '\t' => "\\t",
            < 0x20 => $"\\u{unicodeScalar:X4}",
            _ => null,
        };
        if (escaped is null)
        {
            return new Rune(unicodeScalar).TryEncodeToUtf16(destination, out numberOfCharactersWritten);
        }
        numberOfCharactersWritten = escaped.TryCopyTo(destination) ? escaped.Length : 0;
        return numberOfCharactersWritten != 0;
    }
}
