using System.Buffers;

namespace Harbor.Core.Ipc;

/// <summary>
/// Splits a pipe's byte stream into '\n'-terminated lines, the framing used by every Harbor pipe.
/// A line longer than <see cref="MaxLineBytes"/> is dropped up to and including its '\n', so one bad
/// message can't grow memory without bound or desynchronise the stream. Mirrors the dock's LineFramer.
/// </summary>
public sealed class LineFramer
{
    public const int MaxLineBytes = 64 * 1024;

    private readonly ArrayBufferWriter<byte> _pending = new();
    private bool _discarding;

    /// <summary>Number of oversized lines dropped so far, for logging.</summary>
    public int DroppedLineCount { get; private set; }

    /// <summary>Appends bytes and returns every line they complete, without the '\n'.</summary>
    public IReadOnlyList<byte[]> Push(ReadOnlySpan<byte> bytes)
    {
        var lines = new List<byte[]>();
        while (!bytes.IsEmpty)
        {
            var newline = bytes.IndexOf((byte)'\n');
            var chunk = newline < 0 ? bytes : bytes[..newline];

            if (!_discarding)
            {
                if (_pending.WrittenCount + chunk.Length > MaxLineBytes)
                {
                    _discarding = true;
                    _pending.ResetWrittenCount();
                }
                else
                {
                    _pending.Write(chunk);
                }
            }

            if (newline < 0)
            {
                break;
            }

            if (_discarding)
            {
                _discarding = false;
                DroppedLineCount++;
            }
            else
            {
                lines.Add(_pending.WrittenSpan.ToArray());
                _pending.ResetWrittenCount();
            }
            bytes = bytes[(newline + 1)..];
        }
        return lines;
    }
}
