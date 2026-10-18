using Harbor.Core.Logging;
using Harbor.Core.Media;
using Windows.Media.Control;
using Windows.Storage.Streams;

namespace Harbor.Platform.Media;

/// <summary>
/// What is playing, from Windows' media sessions (GlobalSystemMediaTransportControlsSessionManager, docs/adr/0016;
/// works unpackaged, M4 spike): the current session's title, artist, playback and timeline, by events only. The
/// artwork is read only while the island wants it. Titles are never logged.
/// </summary>
public sealed partial class GsmtcMediaSource(ILog log) : IMediaSource
{
    private const int MaxArtworkBytes = 2 * 1024 * 1024;
    private readonly Lock _lock = new();
    private GlobalSystemMediaTransportControlsSessionManager? _manager;
    private GlobalSystemMediaTransportControlsSession? _session;
    private MediaState? _current;
    private bool _artworkWanted;
    private int _generation;
    private bool _disposed;

    public MediaState? Current
    {
        get
        {
            lock (_lock)
            {
                return _current;
            }
        }
    }

    public event Action? Changed;

    public bool ArtworkWanted
    {
        get => Volatile.Read(ref _artworkWanted);
        set
        {
            var was = Volatile.Read(ref _artworkWanted);
            Volatile.Write(ref _artworkWanted, value);
            if (value && !was)
            {
                _ = RefreshAsync();
            }
        }
    }

    public async Task StartAsync()
    {
        try
        {
            _manager = await GlobalSystemMediaTransportControlsSessionManager.RequestAsync();
            _manager.CurrentSessionChanged += (_, _) => Follow(_manager.GetCurrentSession());
            Follow(_manager.GetCurrentSession());
        }
        catch (Exception exception) when (exception is System.Runtime.InteropServices.COMException or UnauthorizedAccessException)
        {
            // No media sessions here: the section shows that nothing is playing.
            log.Warn($"media: sessions aren't available ({exception.HResult:X8})");
        }
    }

    private void Follow(GlobalSystemMediaTransportControlsSession? session)
    {
        lock (_lock)
        {
            if (_disposed || ReferenceEquals(session, _session))
            {
                return;
            }
            if (_session is { } old)
            {
                old.MediaPropertiesChanged -= OnSessionChanged;
                old.PlaybackInfoChanged -= OnSessionChanged;
                old.TimelinePropertiesChanged -= OnSessionChanged;
            }
            _session = session;
            if (session is not null)
            {
                session.MediaPropertiesChanged += OnSessionChanged;
                session.PlaybackInfoChanged += OnSessionChanged;
                session.TimelinePropertiesChanged += OnSessionChanged;
            }
        }
        // Whether there is one, never what it plays.
        log.Info(session is null ? "media: no session" : "media: a session to follow");
        _ = RefreshAsync();
    }

    private void OnSessionChanged(GlobalSystemMediaTransportControlsSession sender, object args) => _ = RefreshAsync();

    private async Task RefreshAsync()
    {
        GlobalSystemMediaTransportControlsSession? session;
        int generation;
        lock (_lock)
        {
            session = _session;
            generation = ++_generation;
        }
        MediaState? state = null;
        if (session is not null)
        {
            try
            {
                var properties = await session.TryGetMediaPropertiesAsync();
                var playback = session.GetPlaybackInfo();
                var timeline = session.GetTimelineProperties();
                var controls = playback?.Controls;
                var duration = timeline is null ? (TimeSpan?)null : timeline.EndTime - timeline.StartTime;
                state = new MediaState(
                    properties?.Title ?? "",
                    properties?.Artist ?? "",
                    playback?.PlaybackStatus == GlobalSystemMediaTransportControlsSessionPlaybackStatus.Playing,
                    controls?.IsPlayPauseToggleEnabled ?? false,
                    controls?.IsPreviousEnabled ?? false,
                    controls?.IsNextEnabled ?? false,
                    timeline is null ? null : timeline.Position - timeline.StartTime,
                    duration > TimeSpan.Zero ? duration : null,
                    timeline is null ? DateTime.UtcNow : timeline.LastUpdatedTime.UtcDateTime,
                    ArtworkWanted && properties?.Thumbnail is { } thumbnail ? await ReadAsync(thumbnail) : null);
            }
            catch (Exception exception) when (exception is System.Runtime.InteropServices.COMException or InvalidOperationException)
            {
                // The session went away while it was being read.
                state = null;
            }
        }
        lock (_lock)
        {
            if (generation != _generation || _disposed)
            {
                return;
            }
            _current = state;
        }
        Changed?.Invoke();
    }

    private static async Task<byte[]?> ReadAsync(IRandomAccessStreamReference thumbnail)
    {
        using var stream = await thumbnail.OpenReadAsync();
        if (stream.Size is 0 or > MaxArtworkBytes)
        {
            return null;
        }
        var buffer = new Windows.Storage.Streams.Buffer((uint)stream.Size);
        var read = await stream.ReadAsync(buffer, (uint)stream.Size, InputStreamOptions.None);
        using var reader = DataReader.FromBuffer(read);
        var bytes = new byte[read.Length];
        reader.ReadBytes(bytes);
        return bytes;
    }

    public Task<bool> PlayPauseAsync() => Do(session => session.TryTogglePlayPauseAsync());

    public Task<bool> NextAsync() => Do(session => session.TrySkipNextAsync());

    public Task<bool> PreviousAsync() => Do(session => session.TrySkipPreviousAsync());

    private async Task<bool> Do(Func<GlobalSystemMediaTransportControlsSession, Windows.Foundation.IAsyncOperation<bool>> action)
    {
        GlobalSystemMediaTransportControlsSession? session;
        lock (_lock)
        {
            session = _session;
        }
        if (session is null)
        {
            return false;
        }
        try
        {
            return await action(session);
        }
        catch (System.Runtime.InteropServices.COMException)
        {
            return false;
        }
    }

    public void Dispose()
    {
        lock (_lock)
        {
            _disposed = true;
            if (_session is { } session)
            {
                session.MediaPropertiesChanged -= OnSessionChanged;
                session.PlaybackInfoChanged -= OnSessionChanged;
                session.TimelinePropertiesChanged -= OnSessionChanged;
            }
            _session = null;
            _current = null;
        }
    }
}
