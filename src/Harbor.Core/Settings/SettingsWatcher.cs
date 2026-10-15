using System.Threading.Channels;
using Harbor.Core.Logging;

namespace Harbor.Core.Settings;

/// <summary>
/// Follows <c>settings.json</c> while Harbor Shell runs. Each good version becomes <see cref="Current"/>. A version
/// that doesn't decode (an editing mistake, or a save caught halfway) is reported as a <see cref="Problem"/> and
/// the last good settings stay, so the dock never flashes back to the defaults. With no good version at all (a
/// broken file at start), <see cref="Current"/> is the defaults.
/// </summary>
public sealed class SettingsWatcher(SettingsFile file, ILog log)
{
    private readonly Channel<bool> _signals = Channel.CreateUnbounded<bool>(new UnboundedChannelOptions { SingleReader = true });
    private volatile HarborSettings _current = HarborSettings.Defaults;
    private volatile string? _problem;

    /// <summary>Editors save in several steps (truncate, write, rename): wait until the file has been quiet this long.</summary>
    public TimeSpan Settle { get; init; } = TimeSpan.FromMilliseconds(300);

    public HarborSettings Current => _current;

    /// <summary>Why settings.json isn't in effect, for people; null when it is.</summary>
    public string? Problem => _problem;

    /// <summary>Raised on the watcher's loop with each new <see cref="Current"/>, the first time included.</summary>
    public event Action<HarborSettings>? Changed;

    /// <summary>Raised on the watcher's loop when <see cref="Problem"/> changes.</summary>
    public event Action<string?>? ProblemChanged;

    /// <summary>Reads the settings, then follows the file until cancelled.</summary>
    public async Task RunAsync(CancellationToken cancellationToken)
    {
        await ReloadAsync(first: true, cancellationToken).ConfigureAwait(false);

        using var watcher = new FileSystemWatcher(file.DataDir, HarborSettings.FileName)
        {
            NotifyFilter = NotifyFilters.FileName | NotifyFilters.LastWrite | NotifyFilters.Size | NotifyFilters.CreationTime,
        };
        watcher.Changed += (_, _) => Signal();
        watcher.Created += (_, _) => Signal();
        watcher.Deleted += (_, _) => Signal();
        watcher.Renamed += (_, _) => Signal();
        // The watcher's buffer overflowed: some events are lost, so read again.
        watcher.Error += (_, _) => Signal();
        watcher.EnableRaisingEvents = true;
        // A save between the first read and the watcher starting would go unnoticed otherwise.
        Signal();

        while (await _signals.Reader.WaitToReadAsync(cancellationToken).ConfigureAwait(false))
        {
            do
            {
                while (_signals.Reader.TryRead(out _))
                {
                }
                await Task.Delay(Settle, cancellationToken).ConfigureAwait(false);
            }
            while (_signals.Reader.TryRead(out _));
            await ReloadAsync(first: false, cancellationToken).ConfigureAwait(false);
        }
    }

    private void Signal() => _signals.Writer.TryWrite(true);

    private async Task ReloadAsync(bool first, CancellationToken cancellationToken)
    {
        // A deleted file comes back with the defaults; until the Shell restarts, the last good list stays.
        var result = File.Exists(file.FilePath) || first
            ? await file.ReadAsync(cancellationToken).ConfigureAwait(false)
            : new SettingsReadResult(null, "it was deleted; it comes back with the defaults when Harbor Shell starts");

        if (result.Problem != _problem)
        {
            _problem = result.Problem;
            if (result.Problem is not null)
            {
                log.Warn($"settings: {file.FilePath} isn't used because {result.Problem}; keeping the last good settings");
            }
            else if (!first)
            {
                log.Info("settings: the problem with settings.json is fixed");
            }
            ProblemChanged?.Invoke(result.Problem);
        }

        if (result.Settings is { } settings && (first || !settings.Equals(_current)))
        {
            _current = settings;
            if (!first)
            {
                log.Info($"settings: read {file.FilePath} again ({settings.Pinned.Count} pinned apps)");
            }
            Changed?.Invoke(settings);
        }
        else if (first)
        {
            // Nothing good to keep: the defaults, until the file is fixed.
            Changed?.Invoke(_current);
        }
    }
}
