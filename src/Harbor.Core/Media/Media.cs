using System.Globalization;

namespace Harbor.Core.Media;

/// <summary>
/// What is playing (docs/adr/0016): the current media session as Windows reports it. Never logged: what someone
/// listens to is their business.
/// </summary>
/// <param name="Position">Where playback was when it was reported (<paramref name="ReportedUtc"/>).</param>
/// <param name="Artwork">The thumbnail as an image file's bytes (PNG, JPEG...), read only while the island shows it.</param>
public sealed record MediaState(
    string Title,
    string Artist,
    bool Playing,
    bool CanPlayPause,
    bool CanPrevious,
    bool CanNext,
    TimeSpan? Position,
    TimeSpan? Duration,
    DateTime ReportedUtc,
    byte[]? Artwork = null)
{
    /// <summary>Where playback is at nowUtc: the reported position, plus the time since while playing, within the duration.</summary>
    public TimeSpan? PositionAt(DateTime nowUtc)
    {
        if (Position is not { } position)
        {
            return null;
        }
        var now = Playing ? position + (nowUtc - ReportedUtc) : position;
        if (now < TimeSpan.Zero)
        {
            now = TimeSpan.Zero;
        }
        return Duration is { } duration && duration > TimeSpan.Zero && now > duration ? duration : now;
    }

    /// <summary>How far along, 0-1; null without a duration.</summary>
    public double? ProgressAt(DateTime nowUtc) =>
        Duration is { } duration && duration > TimeSpan.Zero && PositionAt(nowUtc) is { } position
            ? position.TotalSeconds / duration.TotalSeconds
            : null;

    /// <summary>A playback time for people: "0:07", "3:45", "1:02:03".</summary>
    public static string Clock(TimeSpan time) =>
        time.TotalHours >= 1
            ? string.Format(CultureInfo.InvariantCulture, "{0}:{1:00}:{2:00}", (int)time.TotalHours, time.Minutes, time.Seconds)
            : string.Format(CultureInfo.InvariantCulture, "{0}:{1:00}", (int)time.TotalMinutes, time.Seconds);
}

/// <summary>
/// Port: the current media session (docs/adr/0016), by events only, no polling. Its controls do what the media
/// keys would.
/// </summary>
public interface IMediaSource : IDisposable
{
    /// <summary>What is playing, or null.</summary>
    MediaState? Current { get; }

    /// <summary>Current changed. Raised on any thread.</summary>
    event Action? Changed;

    /// <summary>Read the artwork too (only while the island shows it).</summary>
    bool ArtworkWanted { get; set; }

    Task StartAsync();

    Task<bool> PlayPauseAsync();

    Task<bool> NextAsync();

    Task<bool> PreviousAsync();
}

/// <summary>Test Shells: nothing is ever playing, so tests never depend on (or touch) the machine's media.</summary>
public sealed class NoMediaSource : IMediaSource
{
    public MediaState? Current => null;

    public event Action? Changed
    {
        add { }
        remove { }
    }

    public bool ArtworkWanted { get; set; }

    public Task StartAsync() => Task.CompletedTask;

    public Task<bool> PlayPauseAsync() => Task.FromResult(false);

    public Task<bool> NextAsync() => Task.FromResult(false);

    public Task<bool> PreviousAsync() => Task.FromResult(false);

    public void Dispose()
    {
    }
}
