using Harbor.Core.Interop;
using Harbor.Core.Settings;

namespace Harbor.Core.Apps;

/// <summary>
/// Looks pinned apps up through the Windows shell: an AppUserModelID as <c>shell:AppsFolder\&lt;AUMID&gt;</c>
/// (packaged and desktop apps alike), a path as the file itself. Icons come with a later step; until then
/// every item has none.
/// </summary>
public sealed class ShellPinResolver : IPinResolver
{
    public ResolvedPin? Resolve(Pin pin)
    {
        ArgumentNullException.ThrowIfNull(pin);
        if (pin.Kind == PinKind.Path && !File.Exists(pin.Target))
        {
            return null;
        }
        var item = ShellItems.FromParsingName(pin.Kind == PinKind.App ? @"shell:AppsFolder\" + pin.Target : pin.Target);
        if (item is null)
        {
            return null;
        }
        try
        {
            return new ResolvedPin(item.GetDisplayName(ShellItemDisplayName.NormalDisplay, out var name) >= 0 ? name : null, Icon: null);
        }
        finally
        {
            ShellItems.Release(item);
        }
    }
}
