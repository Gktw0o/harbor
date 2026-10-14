using System.Collections.Concurrent;
using Harbor.Core.Logging;

namespace Harbor.Core.Tests;

// ILog that keeps every message for assertions.
internal sealed class CapturingLog : ILog
{
    public ConcurrentQueue<string> Lines { get; } = new();

    public void Write(LogLevel level, string message) => Lines.Enqueue($"{level}: {message}");

    public bool Contains(string text) => Lines.Any(line => line.Contains(text, StringComparison.Ordinal));
}
