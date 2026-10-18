using System.Globalization;

namespace Harbor.Core.Ipc;

/// <summary>
/// Names of Harbor's named pipes. Must stay identical to the dock's <c>PipeName</c>
/// (src/dock/core/ipc/pipe_name.h); both sides test the same cases.
/// </summary>
public static class PipeNames
{
    /// <summary>Channel carrying control messages (ping/pong) between Harbor.Shell and the dock.</summary>
    public const string ControlChannel = "control";

    /// <summary>
    /// Pipe name for one channel in one Windows session, e.g. <c>harbor.control.3</c>
    /// (without the <c>\\.\pipe\</c> prefix, as <see cref="System.IO.Pipes"/> expects).
    /// </summary>
    public static string For(string channel, uint sessionId) =>
        string.Create(CultureInfo.InvariantCulture, $"harbor.{channel}.{sessionId}");

    /// <summary>
    /// True for names Harbor may serve or connect to: <c>harbor.</c> followed by 1-200 characters from
    /// [a-z0-9.-], as the dock's <c>IsValidPipeName</c>. Guards the <c>--pipe-name</c> override used by tests.
    /// </summary>
    public static bool IsValid(string name)
    {
        ArgumentNullException.ThrowIfNull(name);
        const string Prefix = "harbor.";
        if (!name.StartsWith(Prefix, StringComparison.Ordinal))
        {
            return false;
        }
        var suffix = name.AsSpan(Prefix.Length);
        if (suffix.IsEmpty || suffix.Length > 200)
        {
            return false;
        }
        foreach (var c in suffix)
        {
            if (!(c is >= 'a' and <= 'z' or >= '0' and <= '9' or '.' or '-'))
            {
                return false;
            }
        }
        return true;
    }
}
