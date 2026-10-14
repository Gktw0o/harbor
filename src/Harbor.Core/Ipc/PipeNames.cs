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
}
