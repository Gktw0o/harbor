using System.Text;
using Harbor.Core.Ipc;
using Harbor.Core.Logging;
using Harbor.Core.Settings;

namespace Harbor.Core.Apps;

/// <param name="Name">The app's display name, if Windows has one.</param>
/// <param name="Icon">Its icon file in the icons folder (contracts/README.md), if one was made.</param>
public sealed record ResolvedPin(string? Name, string? Icon);

/// <summary>Port: what Windows knows about a pinned app.</summary>
public interface IPinResolver
{
    /// <summary>Null if the app isn't installed or the file doesn't exist. May block (shell calls); never throws for a missing app.</summary>
    ResolvedPin? Resolve(Pin pin);
}

/// <summary>Turns the pinned apps into the dock's list (dockItems): apps that can't be found are left off.</summary>
public static class DockItemsBuilder
{
    public static DockItems Build(HarborSettings settings, IPinResolver resolver, ILog log)
    {
        ArgumentNullException.ThrowIfNull(settings);
        ArgumentNullException.ThrowIfNull(resolver);
        var items = new List<DockItem>(settings.Pinned.Count);
        for (var index = 0; index < settings.Pinned.Count && items.Count < DockItems.MaxItems; index++)
        {
            var pin = settings.Pinned[index];
            ResolvedPin? resolved;
            try
            {
                resolved = resolver.Resolve(pin);
            }
            catch (Exception exception) when (exception is not OutOfMemoryException)
            {
                log.Warn($"dock items: pinned[{index}] couldn't be looked up ({exception.Message}); left off the dock");
                continue;
            }
            if (resolved is null)
            {
                // Which app isn't logged: the pinned list is the user's business.
                log.Info($"dock items: pinned[{index}] isn't installed or doesn't exist; left off the dock");
                continue;
            }
            var icon = resolved.Icon is { } file && MessageCodec.IsIconFileName(file) ? file : null;
            items.Add(new DockItem(
                pin.Kind == PinKind.Path ? DockItemKind.Path : DockItemKind.App,
                pin.Target,
                CleanName(resolved.Name) ?? FallbackName(pin),
                icon));
        }
        return new DockItems(items);
    }

    /// <summary>
    /// A name dockItems accepts: control characters become spaces, broken UTF-16 becomes U+FFFD, and it is cut to
    /// <see cref="DockItems.MaxNameLength"/> characters. Null if nothing is left.
    /// </summary>
    public static string? CleanName(string? name)
    {
        if (name is null)
        {
            return null;
        }
        var cleaned = new StringBuilder(Math.Min(name.Length, DockItems.MaxNameLength * 2));
        var rest = name.AsSpan();
        for (var length = 0; !rest.IsEmpty && length < DockItems.MaxNameLength; length++)
        {
            // An unpaired surrogate decodes as U+FFFD.
            _ = Rune.DecodeFromUtf16(rest, out var rune, out var used);
            cleaned.Append(rune.Value < 0x20 ? " " : rune.ToString());
            rest = rest[used..];
        }
        var trimmed = cleaned.ToString().Trim();
        return trimmed.Length == 0 ? null : trimmed;
    }

    // Without a name from Windows: the file name, or the AppUserModelID itself.
    private static string FallbackName(Pin pin) =>
        CleanName(pin.Kind == PinKind.Path ? Path.GetFileNameWithoutExtension(pin.Target) : pin.Target) ?? "App";
}
