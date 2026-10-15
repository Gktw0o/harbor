using System.Runtime.InteropServices;
using System.Runtime.InteropServices.Marshalling;
using Harbor.Core.Interop;

namespace Harbor.Core.Tests;

// Writes real shortcut (.lnk) files through the shell's ShellLink object, for the tests that read them.
internal static partial class Shortcuts
{
    private static readonly Guid ShellLinkClass = new("00021401-0000-0000-c000-000000000046");
    private static readonly Guid ShellLinkId = new("000214f9-0000-0000-c000-000000000046");

    // A shortcut at path to target, carrying aumid if given.
    public static void Create(string path, string target, string? aumid = null)
    {
        Marshal.ThrowExceptionForHR(CoCreateInstance(ShellLinkClass, 0, 1 /* CLSCTX_INPROC_SERVER */, ShellLinkId, out var pointer));
        var link = (IShellLinkW)new StrategyBasedComWrappers().GetOrCreateObjectForComInstance(pointer, CreateObjectFlags.None);
        Marshal.Release(pointer);
        Marshal.ThrowExceptionForHR(link.SetPath(target));
        if (aumid is not null)
        {
            var store = (IPropertyStore)(object)link;
            var value = new PropVariant { Type = PropVariant.WideString, Value = Marshal.StringToCoTaskMemUni(aumid) };
            try
            {
                Marshal.ThrowExceptionForHR(store.SetValue(PropertyKey.AppUserModelId, value));
                Marshal.ThrowExceptionForHR(store.Commit());
            }
            finally
            {
                Marshal.FreeCoTaskMem(value.Value);
            }
        }
        Marshal.ThrowExceptionForHR(((IPersistFile)(object)link).Save(path, remember: 1));
        ((ComObject)(object)link).FinalRelease();
    }

    [LibraryImport("ole32.dll")]
    private static partial int CoCreateInstance(in Guid classId, nint outer, uint context, in Guid interfaceId, out nint result);

    // IShellLinkW up to SetPath: every slot before it must be there.
    [GeneratedComInterface]
    [Guid("000214f9-0000-0000-c000-000000000046")]
    internal partial interface IShellLinkW
    {
        [PreserveSig] int GetPath(nint file, int size, nint findData, uint flags);
        [PreserveSig] int GetIDList(out nint list);
        [PreserveSig] int SetIDList(nint list);
        [PreserveSig] int GetDescription(nint name, int size);
        [PreserveSig] int SetDescription(nint name);
        [PreserveSig] int GetWorkingDirectory(nint directory, int size);
        [PreserveSig] int SetWorkingDirectory(nint directory);
        [PreserveSig] int GetArguments(nint arguments, int size);
        [PreserveSig] int SetArguments(nint arguments);
        [PreserveSig] int GetHotkey(out ushort hotkey);
        [PreserveSig] int SetHotkey(ushort hotkey);
        [PreserveSig] int GetShowCmd(out int show);
        [PreserveSig] int SetShowCmd(int show);
        [PreserveSig] int GetIconLocation(nint path, int size, out int index);
        [PreserveSig] int SetIconLocation(nint path, int index);
        [PreserveSig] int SetRelativePath(nint path, uint reserved);
        [PreserveSig] int Resolve(nint window, uint flags);

        [PreserveSig]
        int SetPath([MarshalAs(UnmanagedType.LPWStr)] string file);
    }

    // IPersistFile, with IPersist's GetClassID first.
    [GeneratedComInterface]
    [Guid("0000010b-0000-0000-c000-000000000046")]
    internal partial interface IPersistFile
    {
        [PreserveSig] int GetClassID(out Guid classId);
        [PreserveSig] int IsDirty();
        [PreserveSig] int Load(nint file, uint mode);

        [PreserveSig]
        int Save([MarshalAs(UnmanagedType.LPWStr)] string file, int remember);
    }
}
