using System.Runtime.InteropServices;
using System.Runtime.InteropServices.Marshalling;

namespace Harbor.Core.Interop;

/// <summary>PROPERTYKEY: a property's format id and number.</summary>
[StructLayout(LayoutKind.Sequential)]
internal readonly struct PropertyKey(Guid formatId, uint id)
{
    public readonly Guid FormatId = formatId;
    public readonly uint Id = id;

    /// <summary>PKEY_AppUserModel_ID.</summary>
    public static PropertyKey AppUserModelId { get; } = new(new Guid("9f4c2855-9f79-4b39-a8d0-e1d42de1d5f3"), 5);

    /// <summary>PKEY_AppUserModel_RelaunchDisplayNameResource: the name a window's app wants it relaunched under.</summary>
    public static PropertyKey RelaunchDisplayName { get; } = new(new Guid("9f4c2855-9f79-4b39-a8d0-e1d42de1d5f3"), 4);

    /// <summary>PKEY_Link_TargetParsingPath: what a shortcut points at.</summary>
    public static PropertyKey LinkTarget { get; } = new(new Guid("b9b4b3fc-2b51-4a42-b5d8-324146afcf25"), 2);
}

/// <summary>
/// IShellItem2's whole vtable up to GetString (IShellItem's five methods first): a slot left out would shift the
/// rest. Only GetString is called.
/// </summary>
[GeneratedComInterface]
[Guid("7e9fb0d3-919f-4307-ab2e-9b1860310c93")]
internal partial interface IShellItem2
{
    [PreserveSig] int BindToHandler(nint bindContext, in Guid handler, in Guid interfaceId, out nint result);
    [PreserveSig] int GetParent(out nint parent);
    [PreserveSig] int GetDisplayName(int kind, out nint name);
    [PreserveSig] int GetAttributes(uint mask, out uint attributes);
    [PreserveSig] int Compare(nint item, uint hint, out int order);
    [PreserveSig] int GetPropertyStore(int flags, in Guid interfaceId, out nint store);
    [PreserveSig] int GetPropertyStoreWithCreateObject(int flags, nint createObject, in Guid interfaceId, out nint store);
    [PreserveSig] int GetPropertyStoreForKeys(nint keys, uint count, int flags, in Guid interfaceId, out nint store);
    [PreserveSig] int GetPropertyDescriptionList(in PropertyKey key, in Guid interfaceId, out nint list);
    [PreserveSig] int Update(nint bindContext);
    [PreserveSig] int GetProperty(in PropertyKey key, nint value);
    [PreserveSig] int GetCLSID(in PropertyKey key, out Guid clsid);
    [PreserveSig] int GetFileTime(in PropertyKey key, out long fileTime);
    [PreserveSig] int GetInt32(in PropertyKey key, out int value);

    /// <summary>value: a CoTaskMemAlloc'd string the caller frees.</summary>
    [PreserveSig] int GetString(in PropertyKey key, out nint value);
}

/// <summary>IPropertyStore. GetValue fills a PROPVARIANT the caller clears.</summary>
[GeneratedComInterface]
[Guid("886d8eeb-8cf2-4446-8d02-cdba1dbdcf99")]
internal partial interface IPropertyStore
{
    [PreserveSig] int GetCount(out uint count);
    [PreserveSig] int GetAt(uint index, out PropertyKey key);
    [PreserveSig] int GetValue(in PropertyKey key, out PropVariant value);
    [PreserveSig] int SetValue(in PropertyKey key, in PropVariant value);
    [PreserveSig] int Commit();
}

/// <summary>PROPVARIANT on 64-bit Windows (the only kind Harbor builds for): a type, then the value.</summary>
[StructLayout(LayoutKind.Sequential)]
internal struct PropVariant
{
    public ushort Type;
    private readonly ushort _reserved1;
    private readonly ushort _reserved2;
    private readonly ushort _reserved3;
    public nint Value;
    private readonly nint _padding;

    /// <summary>VT_LPWSTR.</summary>
    public const ushort WideString = 31;
}

/// <summary>Reads shell properties: a shortcut's target and AppUserModelID, a window's relaunch name.</summary>
internal static partial class Properties
{
    private static readonly Guid ShellItem2Id = new("7e9fb0d3-919f-4307-ab2e-9b1860310c93");
    private static readonly Guid PropertyStoreId = new("886d8eeb-8cf2-4446-8d02-cdba1dbdcf99");

    /// <summary>A string property of a file (a shortcut's target, say), or null.</summary>
    public static string? OfFile(string path, PropertyKey key)
    {
        if (SHCreateItemFromParsingName(path, 0, ShellItem2Id, out var item) < 0 || item is null)
        {
            return null;
        }
        try
        {
            if (item.GetString(key, out var value) < 0 || value == 0)
            {
                return null;
            }
            try
            {
                return Marshal.PtrToStringUni(value);
            }
            finally
            {
                Marshal.FreeCoTaskMem(value);
            }
        }
        finally
        {
            ((ComObject)(object)item).FinalRelease();
        }
    }

    /// <summary>
    /// A string property a window carries (its AppUserModelID, relaunch name...), or null. Reading a window's
    /// properties doesn't send it messages, so a hung window can't block this.
    /// </summary>
    public static string? OfWindow(nint window, PropertyKey key)
    {
        if (SHGetPropertyStoreForWindow(window, PropertyStoreId, out var store) < 0 || store is null)
        {
            return null;
        }
        try
        {
            if (store.GetValue(key, out var value) < 0)
            {
                return null;
            }
            try
            {
                return value.Type == PropVariant.WideString && value.Value != 0 ? Marshal.PtrToStringUni(value.Value) : null;
            }
            finally
            {
                _ = PropVariantClear(ref value);
            }
        }
        finally
        {
            ((ComObject)(object)store).FinalRelease();
        }
    }

    /// <summary>"@dll,-id" resource strings as their text; anything else as is. Null if it can't be loaded.</summary>
    public static unsafe string? LoadIndirect(string text)
    {
        if (!text.StartsWith('@'))
        {
            return text;
        }
        var buffer = stackalloc char[1024];
        return SHLoadIndirectString(text, buffer, 1024, 0) >= 0 ? new string(buffer) : null;
    }

    [LibraryImport("shell32.dll", StringMarshalling = StringMarshalling.Utf16)]
    private static partial int SHCreateItemFromParsingName(string path, nint bindContext, in Guid interfaceId, out IShellItem2? item);

    [LibraryImport("shell32.dll")]
    private static partial int SHGetPropertyStoreForWindow(nint window, in Guid interfaceId, out IPropertyStore? store);

    [LibraryImport("ole32.dll")]
    private static partial int PropVariantClear(ref PropVariant value);

    [LibraryImport("shlwapi.dll", StringMarshalling = StringMarshalling.Utf16)]
    private static unsafe partial int SHLoadIndirectString(string source, char* output, uint size, nint reserved);
}
