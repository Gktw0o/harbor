using System.Diagnostics;
using System.Runtime.InteropServices;
using Harbor.Core.Interop;

namespace Harbor.Core.Apps;

/// <param name="Name">The app's display name, if one was found.</param>
/// <param name="Icon">Its icon file in the icons folder (contracts/README.md), if one was made.</param>
public sealed record ResolvedApp(string? Name, string? Icon);

/// <param name="Target">The path the shortcut points at, if it points at a file.</param>
/// <param name="Aumid">The AppUserModelID the shortcut carries, if any.</param>
public sealed record ShortcutInfo(string? Target, string? Aumid);

/// <summary>Port: what Windows knows about a running app (docs/adr/0010). Calls may block (shell calls); none throws for a missing app.</summary>
public interface IAppResolver
{
    /// <summary>The app with this AppUserModelID in Start (<c>shell:AppsFolder</c>), or null if Start doesn't know it.</summary>
    ResolvedApp? ResolveAumid(string aumid);

    /// <summary>The executable: its description (or file name) and icon; null if the file isn't there.</summary>
    ResolvedApp? ResolveExe(string path);

    /// <summary>The name a window's app asked it to be relaunched under (browser profiles), or null.</summary>
    string? WindowRelaunchName(uint window);

    /// <summary>What a shortcut (.lnk) points at; null if it can't be read.</summary>
    ShortcutInfo? ReadShortcut(string path);
}

/// <summary>
/// <see cref="IAppResolver"/> through the Windows shell, with icons saved to <paramref name="icons"/> at 256 pixels
/// (none without a store).
/// </summary>
public sealed class ShellAppResolver(IconStore? icons = null) : IAppResolver
{
    public ResolvedApp? ResolveAumid(string aumid)
    {
        ArgumentNullException.ThrowIfNull(aumid);
        return FromShellItem(@"shell:AppsFolder\" + aumid, name: null);
    }

    public ResolvedApp? ResolveExe(string path)
    {
        ArgumentNullException.ThrowIfNull(path);
        if (!File.Exists(path))
        {
            return null;
        }
        // The description people know the app by ("Windows Command Processor"); the shell's name is the file's.
        string? description = null;
        try
        {
            description = FileVersionInfo.GetVersionInfo(path).FileDescription;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
        }
        return FromShellItem(path, string.IsNullOrWhiteSpace(description) ? Path.GetFileNameWithoutExtension(path) : description);
    }

    public string? WindowRelaunchName(uint window)
    {
        var resource = Properties.OfWindow((nint)window, PropertyKey.RelaunchDisplayName);
        return resource is null ? null : Properties.LoadIndirect(resource);
    }

    public ShortcutInfo? ReadShortcut(string path)
    {
        ArgumentNullException.ThrowIfNull(path);
        if (!File.Exists(path))
        {
            return null;
        }
        return new ShortcutInfo(Properties.OfFile(path, PropertyKey.LinkTarget), Properties.OfFile(path, PropertyKey.AppUserModelId));
    }

    // name: what to call it; null for the shell's display name.
    private ResolvedApp? FromShellItem(string parsingName, string? name)
    {
        var item = ShellItems.FromParsingName(parsingName);
        if (item is null)
        {
            return null;
        }
        try
        {
            if (name is null && item.GetDisplayName(ShellItemDisplayName.NormalDisplay, out var displayName) >= 0)
            {
                name = displayName;
            }
            return new ResolvedApp(name, SaveIcon(item));
        }
        finally
        {
            ShellItems.Release(item);
        }
    }

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
