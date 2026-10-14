namespace Harbor.Core.Logging;

/// <summary>Where diagnostics go. Implementations are thread-safe and never throw.</summary>
public interface ILog
{
    void Write(LogLevel level, string message);
}

public static class LogExtensions
{
    public static void Info(this ILog log, string message)
    {
        ArgumentNullException.ThrowIfNull(log);
        log.Write(LogLevel.Info, message);
    }

    public static void Warn(this ILog log, string message)
    {
        ArgumentNullException.ThrowIfNull(log);
        log.Write(LogLevel.Warning, message);
    }

    public static void Error(this ILog log, string message)
    {
        ArgumentNullException.ThrowIfNull(log);
        log.Write(LogLevel.Error, message);
    }
}
