using System.Security;
using Harbor.Core.Interop;
using Microsoft.Win32;

namespace Harbor.Core.Controls;

/// <summary>
/// Light/dark mode: the one undocumented piece, approved in docs/adr/0016. It writes AppsUseLightTheme and
/// SystemUsesLightTheme under the user's Personalize key, then broadcasts "ImmersiveColorSet", as Auto Dark Mode
/// does. If a Windows update changes how this works, remove the switch.
/// </summary>
internal static class RegistryTheme
{
    private const string Personalize = @"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize";
    private const string Apps = "AppsUseLightTheme";
    private const string Windows = "SystemUsesLightTheme";

    /// <summary>
    /// Dark: apps and Windows both dark. Null if the apps' value is missing or not a DWORD (the switch is then
    /// hidden); a missing Windows value counts as light, as Windows does.
    /// </summary>
    public static bool? IsDark(object? apps, object? windows) =>
        apps is int appsLight ? appsLight == 0 && (windows as int? ?? 1) == 0 : null;

    public static bool? ReadDark()
    {
        try
        {
            using var key = Registry.CurrentUser.OpenSubKey(Personalize);
            return key is null ? null : IsDark(key.GetValue(Apps), key.GetValue(Windows));
        }
        catch (Exception exception) when (exception is SecurityException or UnauthorizedAccessException or IOException)
        {
            return null;
        }
    }

    /// <summary>Writes both values and tells every window. Slow (the broadcast): never on a UI thread.</summary>
    public static bool SetDark(bool dark)
    {
        try
        {
            using var key = Registry.CurrentUser.OpenSubKey(Personalize, writable: true);
            if (key is null)
            {
                return false;
            }
            key.SetValue(Apps, dark ? 0 : 1, RegistryValueKind.DWord);
            key.SetValue(Windows, dark ? 0 : 1, RegistryValueKind.DWord);
        }
        catch (Exception exception) when (exception is SecurityException or UnauthorizedAccessException or IOException)
        {
            return false;
        }
        ControlsNative.BroadcastThemeChange();
        return true;
    }
}
