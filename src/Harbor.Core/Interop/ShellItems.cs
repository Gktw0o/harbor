using System.Runtime.InteropServices;
using System.Runtime.InteropServices.Marshalling;

namespace Harbor.Core.Interop;

/// <summary>IShellItem's vtable as far as Harbor uses it; methods after GetDisplayName are left out.</summary>
[GeneratedComInterface]
[Guid("43826d1e-e718-42ee-bc55-a1e261c37bfe")]
internal partial interface IShellItem
{
    [PreserveSig]
    int BindToHandler(nint bindContext, in Guid handler, in Guid interfaceId, out nint result);

    [PreserveSig]
    int GetParent(out nint parent);

    [PreserveSig]
    int GetDisplayName(ShellItemDisplayName kind, [MarshalAs(UnmanagedType.LPWStr)] out string? name);
}

internal enum ShellItemDisplayName
{
    /// <summary>SIGDN_NORMALDISPLAY: the name people see in Explorer.</summary>
    NormalDisplay = 0,
}

internal static partial class ShellItems
{
    private static readonly Guid ShellItemId = new("43826d1e-e718-42ee-bc55-a1e261c37bfe");

    /// <summary>The shell item for a parsing name (a path, or <c>shell:AppsFolder\&lt;AUMID&gt;</c>), or null if there's none.</summary>
    public static IShellItem? FromParsingName(string parsingName) =>
        SHCreateItemFromParsingName(parsingName, 0, ShellItemId, out var item) >= 0 ? item : null;

    /// <summary>Lets go of the COM object now rather than when the GC gets to it.</summary>
    public static void Release(IShellItem item) => ((ComObject)(object)item).FinalRelease();

    [LibraryImport("shell32.dll", StringMarshalling = StringMarshalling.Utf16)]
    private static partial int SHCreateItemFromParsingName(string path, nint bindContext, in Guid interfaceId, out IShellItem? item);
}
