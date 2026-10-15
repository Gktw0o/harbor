using System.Runtime.InteropServices;
using Harbor.Core.Interop;
using Harbor.Core.Settings;

namespace Harbor.Core.Apps;

/// <summary>
/// Looks pinned apps up through the Windows shell: an AppUserModelID as <c>shell:AppsFolder\&lt;AUMID&gt;</c>
/// (packaged and desktop apps alike), a path as the file itself. Their icons go to <paramref name="icons"/> at
/// 256 pixels; without a store, items have no icon.
/// </summary>
public sealed class ShellPinResolver(IconStore? icons = null) : IPinResolver
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
            var name = item.GetDisplayName(ShellItemDisplayName.NormalDisplay, out var displayName) >= 0 ? displayName : null;
            return new ResolvedPin(name, SaveIcon(item));
        }
        finally
        {
            ShellItems.Release(item);
        }
    }

    // An item without an icon still goes on the dock (with a placeholder).
    private string? SaveIcon(IShellItem item)
    {
        if (icons is null)
        {
            return null;
        }
        try
        {
            if (ShellItems.IconOf(item, IconFile.MaxSide) is not { } bitmap)
            {
                return null;
            }
            IconFile.Normalize(bitmap.Pixels);
            return icons.Save(new IconImage(bitmap.Width, bitmap.Height, bitmap.Pixels));
        }
        catch (Exception exception) when (exception is COMException or InvalidCastException)
        {
            return null;
        }
    }
}
