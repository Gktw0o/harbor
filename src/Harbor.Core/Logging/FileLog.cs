using System.Diagnostics;
using System.Text;

namespace Harbor.Core.Logging;

/// <summary>
/// Appends UTF-8 lines to <c>&lt;directory&gt;\&lt;name&gt;.log</c>. A line that would take the file past
/// <c>maxBytes</c> first moves it to <c>&lt;name&gt;.old.log</c> (replacing the previous one), so the logs
/// never use more than about twice <c>maxBytes</c>. With no directory it logs to the debugger only.
/// Thread-safe, and never throws for I/O problems: logging must not take the Shell down.
/// </summary>
public sealed class FileLog
{
    public const long DefaultMaxBytes = 1024 * 1024;

    private readonly Lock _lock = new();
    private readonly string? _path;
    private readonly string? _oldPath;
    private readonly long _maxBytes;

    public FileLog(string? directory, string name, long maxBytes = DefaultMaxBytes)
    {
        _maxBytes = maxBytes;
        if (string.IsNullOrEmpty(directory))
        {
            return;
        }
        _path = Path.Combine(directory, name + ".log");
        _oldPath = Path.Combine(directory, name + ".old.log");
        try
        {
            Directory.CreateDirectory(directory);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            // Shows up as the file not opening; the debugger still gets every line.
        }
    }

    public void Info(string message) => Write(LogLevel.Info, message);

    public void Warn(string message) => Write(LogLevel.Warning, message);

    public void Error(string message) => Write(LogLevel.Error, message);

    public void Write(LogLevel level, string message)
    {
        var line = LogLine.Format(DateTime.UtcNow, level, message);
        Debug.Write(line);
        if (_path is null)
        {
            return;
        }

        var bytes = Encoding.UTF8.GetBytes(line);
        lock (_lock)
        {
            try
            {
                var current = new FileInfo(_path);
                if (current.Exists && current.Length > 0 && current.Length + bytes.Length > _maxBytes)
                {
                    RollOver();
                }
                // Opened per line: lines are rare, and holding no handle leaves nothing to dispose.
                using var file = new FileStream(
                    _path, FileMode.Append, FileAccess.Write, FileShare.ReadWrite | FileShare.Delete, bufferSize: 0);
                file.Write(bytes);
            }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
            {
            }
        }
    }

    // Fails while another process has the file open without FileShare.Delete (e.g. an editor); then the
    // line goes to the current file, and the next line tries again.
    private void RollOver()
    {
        try
        {
            File.Move(_path!, _oldPath!, overwrite: true);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
        }
    }
}
