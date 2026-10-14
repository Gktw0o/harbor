using System.Globalization;
using System.Text;

namespace Harbor.Core.Logging;

public enum LogLevel
{
    Info,
    Warning,
    Error,
}

/// <summary>
/// One log entry as a line: <c>2026-10-14T15:30:00.123Z [info] message\n</c>. CR and LF inside the message
/// are written as <c>\r</c> and <c>\n</c> so each entry stays on one line. Must match the dock's format
/// (src/dock/core/log/log_line.cpp); both sides test the same cases.
/// </summary>
public static class LogLine
{
    public static string Format(DateTime utcTime, LogLevel level, string message)
    {
        ArgumentNullException.ThrowIfNull(message);
        var line = new StringBuilder(message.Length + 40);
        line.Append(utcTime.ToString("yyyy-MM-dd'T'HH:mm:ss.fff'Z'", CultureInfo.InvariantCulture))
            .Append(" [")
            .Append(level switch
            {
                LogLevel.Info => "info",
                LogLevel.Warning => "warn",
                LogLevel.Error => "error",
                _ => "unknown",
            })
            .Append("] ");
        foreach (var c in message)
        {
            switch (c)
            {
                case '\r': line.Append("\\r"); break;
                case '\n': line.Append("\\n"); break;
                default: line.Append(c); break;
            }
        }
        return line.Append('\n').ToString();
    }
}
