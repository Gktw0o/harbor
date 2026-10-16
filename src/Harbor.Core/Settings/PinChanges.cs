using Harbor.Core.Ipc;
using Harbor.Core.Logging;

namespace Harbor.Core.Settings;

/// <param name="Settings">The settings to write; null if there's nothing to write.</param>
/// <param name="Outcome">What happened, for people (never names the app: it goes to the log).</param>
public sealed record PinChangeResult(HarborSettings? Settings, string Outcome);

/// <summary>Pinning and unpinning from the dock (pinRequest, docs/adr/0010).</summary>
public static class PinChanges
{
    /// <summary>
    /// The settings with <paramref name="pin"/> added at the end (nothing if it's pinned already), or with every copy
    /// of it gone. Targets compare without case, as Windows does.
    /// </summary>
    public static PinChangeResult Apply(HarborSettings settings, PinAction action, Pin pin)
    {
        ArgumentNullException.ThrowIfNull(settings);
        ArgumentNullException.ThrowIfNull(pin);
        var pinned = settings.Pinned.Any(existing => Same(existing, pin));
        if (action == PinAction.Unpin)
        {
            return pinned
                ? new PinChangeResult(new HarborSettings([.. settings.Pinned.Where(existing => !Same(existing, pin))]), "unpinned")
                : new PinChangeResult(null, "it isn't pinned");
        }
        if (pinned)
        {
            return new PinChangeResult(null, "it is pinned already");
        }
        if (settings.Pinned.Count >= HarborSettings.MaxPinned)
        {
            return new PinChangeResult(null, $"the dock holds {HarborSettings.MaxPinned} pinned apps");
        }
        var changed = new HarborSettings([.. settings.Pinned, pin]);
        // Only what the file can hold (a path must be absolute, say).
        return HarborSettings.TryDecode(HarborSettings.Encode(changed), out _, out var problem)
            ? new PinChangeResult(changed, "pinned")
            : new PinChangeResult(null, $"it can't be pinned: {problem}");
    }

    /// <summary>Carries out a pin request from the dock on settings.json; the Shell's settings watcher sends the new list.</summary>
    public static async Task<SettingsChangeResult> ApplyAsync(SettingsFile file, PinRequest request, ILog log)
    {
        ArgumentNullException.ThrowIfNull(file);
        ArgumentNullException.ThrowIfNull(request);
        ArgumentNullException.ThrowIfNull(log);
        var pin = new Pin(request.Item.Kind == DockItemKind.Path ? PinKind.Path : PinKind.App, request.Item.Target);
        var result = await file.TryChangeAsync(settings => Apply(settings, request.Action, pin)).ConfigureAwait(false);
        // What happened, not to which app.
        log.Info($"settings: {(request.Action == PinAction.Pin ? "pin" : "unpin")} request from the dock: {result.Outcome}");
        return result;
    }

    private static bool Same(Pin a, Pin b) =>
        a.Kind == b.Kind && string.Equals(a.Target, b.Target, StringComparison.OrdinalIgnoreCase);
}
