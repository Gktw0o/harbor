using System.Threading.Channels;
using Harbor.Core.Ipc;
using Harbor.Core.Logging;

namespace Harbor.Core.Apps;

public sealed record AppLookupOptions
{
    /// <summary>A lookup that takes longer gets an answer from what's known at once, then the real one.</summary>
    public TimeSpan QuickAnswer { get; init; } = TimeSpan.FromMilliseconds(150);

    /// <summary>A lookup still running after this is given up (shell calls on a network path can hang).</summary>
    public TimeSpan Timeout { get; init; } = TimeSpan.FromSeconds(5);
}

/// <summary>
/// Harbor Shell's side of the running-app messages (docs/adr/0010): answers each appLookup with appInfo: the app's
/// name and icon, the pinned item it is, and how to start it. Lookups run off the loop, each with a time limit;
/// results are kept for the Shell's lifetime. When the pins change, every app the dock asked about on this
/// connection is answered again. Logs app ids only.
/// </summary>
public sealed class AppLookupService(IAppResolver resolver, ILog log, IconStore? icons = null, AppLookupOptions? options = null)
{
    private readonly AppLookupOptions _options = options ?? new AppLookupOptions();
    private readonly Channel<Func<Task>> _work = Channel.CreateUnbounded<Func<Task>>(new UnboundedChannelOptions { SingleReader = true });

    // Loop only.
    private readonly Dictionary<uint, AppLookup> _live = [];
    private readonly Dictionary<string, Resolution> _cache = new(StringComparer.Ordinal);
    private List<(AppTarget Pin, HashSet<string> Keys)> _pins = [];

    /// <summary>Raised on the loop with each answer for the dock.</summary>
    public event Action<AppInfo>? Answered;

    /// <summary>The dock asked about an app.</summary>
    public void Lookup(AppLookup lookup)
    {
        ArgumentNullException.ThrowIfNull(lookup);
        Post(() => OnLookup(lookup));
    }

    /// <summary>The pinned items the dock shows now (after dockItems went out).</summary>
    public void SetPins(DockItems pins)
    {
        ArgumentNullException.ThrowIfNull(pins);
        Post(() => OnPinsAsync(pins));
    }

    /// <summary>The dock went away: what it asked about no longer counts (it asks again when it connects).</summary>
    public void Disconnected() => Post(() =>
    {
        _live.Clear();
        icons?.ReleaseHeld();
        return Task.CompletedTask;
    });

    public async Task RunAsync(CancellationToken cancellationToken)
    {
        await foreach (var work in _work.Reader.ReadAllAsync(cancellationToken).ConfigureAwait(false))
        {
            try
            {
                await work().ConfigureAwait(false);
            }
            catch (Exception exception) when (exception is not OperationCanceledException)
            {
                log.Error($"apps: {exception}");
            }
        }
    }

    private void Post(Func<Task> work) => _work.Writer.TryWrite(work);

    private Task OnLookup(AppLookup lookup)
    {
        _live[lookup.Id] = lookup;
        if (CacheKey(lookup) is { } key && _cache.TryGetValue(key, out var known))
        {
            Answer(lookup, known);
            return Task.CompletedTask;
        }
        _ = ResolveAsync(lookup);
        return Task.CompletedTask;
    }

    // Off the loop: posts the answers back to it.
    private async Task ResolveAsync(AppLookup lookup)
    {
        var resolving = Task.Run(() => Resolve(lookup));
        if (await Task.WhenAny(resolving, Task.Delay(_options.QuickAnswer)).ConfigureAwait(false) != resolving)
        {
            Post(() =>
            {
                if (IsLive(lookup))
                {
                    Answer(lookup, new Resolution(null, null, null));
                }
                return Task.CompletedTask;
            });
        }
        var rest = _options.Timeout - _options.QuickAnswer;
        if (await Task.WhenAny(resolving, Task.Delay(rest > TimeSpan.Zero ? rest : TimeSpan.Zero)).ConfigureAwait(false) != resolving)
        {
            log.Warn($"apps: looking up app {lookup.Id} took over {_options.Timeout.TotalSeconds:0.#} s; giving up");
            return;
        }
        if (resolving.Status != TaskStatus.RanToCompletion)
        {
            log.Warn($"apps: looking up app {lookup.Id} failed: {resolving.Exception?.InnerException?.Message}");
            return;
        }
        var resolution = resolving.Result;
        Post(() =>
        {
            if (CacheKey(lookup) is { } key)
            {
                _cache[key] = resolution;
            }
            if (IsLive(lookup))
            {
                Answer(lookup, resolution);
            }
            return Task.CompletedTask;
        });
    }

    // What Windows knows: Start's entry for the AppUserModelID, else the executable, else the window's relaunch name.
    private Resolution Resolve(AppLookup lookup)
    {
        ResolvedApp? app = null;
        AppTarget? launch = null;
        if (lookup.Aumid is { } aumid && (app = resolver.ResolveAumid(aumid)) is not null)
        {
            launch = new AppTarget(DockItemKind.App, aumid);
        }
        if (app is null && lookup.Exe is { } exe && (app = resolver.ResolveExe(exe)) is not null &&
            MessageCodec.IsValidDockItemText(exe, DockItems.MaxTargetLength))
        {
            launch = new AppTarget(DockItemKind.Path, exe);
        }
        var name = DockItemsBuilder.CleanName(app?.Name);
        if (name is null && lookup.Window is { } window)
        {
            name = DockItemsBuilder.CleanName(resolver.WindowRelaunchName(window));
        }
        var icon = app?.Icon is { } file && MessageCodec.IsIconFileName(file) ? file : null;
        return new Resolution(name, icon, launch);
    }

    private async Task OnPinsAsync(DockItems pins)
    {
        // A shortcut's target and AppUserModelID are shell calls too: off the loop, with the same limit.
        var reading = Task.Run(() => pins.Items.Select(item => (new AppTarget(item.Kind, item.Target), KeysOf(item))).ToList());
        if (await Task.WhenAny(reading, Task.Delay(_options.Timeout)).ConfigureAwait(false) != reading ||
            reading.Status != TaskStatus.RanToCompletion)
        {
            log.Warn("apps: reading the pinned shortcuts took too long; pins match by their targets only");
            _pins = [.. pins.Items.Select(item => (new AppTarget(item.Kind, item.Target), DirectKeys(item)))];
        }
        else
        {
            _pins = reading.Result;
        }
        // Which pin each app is may have changed: answer again, after the dockItems that went out first.
        foreach (var lookup in _live.Values)
        {
            Answer(lookup, CacheKey(lookup) is { } key && _cache.TryGetValue(key, out var known) ? known : new Resolution(null, null, null));
        }
    }

    private HashSet<string> KeysOf(DockItem item)
    {
        var keys = DirectKeys(item);
        if (item.Kind == DockItemKind.Path && item.Target.EndsWith(".lnk", StringComparison.OrdinalIgnoreCase) &&
            resolver.ReadShortcut(item.Target) is { } shortcut)
        {
            if (shortcut.Target is { } target)
            {
                keys.Add(ExeKey(target));
            }
            if (shortcut.Aumid is { } aumid)
            {
                keys.Add(AumidKey(aumid));
            }
        }
        return keys;
    }

    private static HashSet<string> DirectKeys(DockItem item) =>
        [item.Kind == DockItemKind.App ? AumidKey(item.Target) : ExeKey(item.Target)];

    private void Answer(AppLookup lookup, Resolution resolution)
    {
        if (resolution.Icon is { } icon)
        {
            icons?.Hold(icon);
        }
        Answered?.Invoke(new AppInfo(lookup.Id, resolution.Name ?? FallbackName(lookup), resolution.Icon, PinOf(lookup), resolution.Launch));
    }

    private AppTarget? PinOf(AppLookup lookup)
    {
        var keys = new List<string>(2);
        if (lookup.Aumid is { } aumid)
        {
            keys.Add(AumidKey(aumid));
        }
        if (lookup.Exe is { } exe)
        {
            keys.Add(ExeKey(exe));
        }
        return _pins.FirstOrDefault(pin => keys.Exists(pin.Keys.Contains)).Pin;
    }

    private bool IsLive(AppLookup lookup) => _live.TryGetValue(lookup.Id, out var live) && live == lookup;

    // Without Windows' name: the executable's file name, else the AppUserModelID.
    private static string FallbackName(AppLookup lookup) =>
        DockItemsBuilder.CleanName(lookup.Exe is { } exe ? Path.GetFileNameWithoutExtension(exe) : lookup.Aumid) ?? "App";

    // Windows compares AppUserModelIDs and paths without case.
    private static string AumidKey(string aumid) => "a:" + aumid.ToUpperInvariant();

    private static string ExeKey(string path) => "e:" + path.ToUpperInvariant();

    // Apps known by neither (a process Windows wouldn't let the dock look into) aren't cached.
    private static string? CacheKey(AppLookup lookup) =>
        lookup.Aumid is { } aumid ? AumidKey(aumid) : lookup.Exe is { } exe ? ExeKey(exe) : null;

    private sealed record Resolution(string? Name, string? Icon, AppTarget? Launch);
}
