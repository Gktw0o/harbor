using System.Runtime.InteropServices;
using System.Runtime.InteropServices.Marshalling;

namespace Harbor.Core.Interop;

// The COM and Win32 calls behind the island's Controls (docs/adr/0016): Core Audio for the volume, WMI for the
// built-in display's brightness, and the theme change broadcast. Each vtable goes as far as Harbor uses it: a
// slot left out would shift the rest.

/// <summary>IMMDeviceEnumerator: the default output device.</summary>
[GeneratedComInterface]
[Guid("a95664d2-9614-4f35-a746-de8db63617e6")]
internal partial interface IMMDeviceEnumerator
{
    [PreserveSig]
    int EnumAudioEndpoints(int dataFlow, int stateMask, out nint devices);

    [PreserveSig]
    int GetDefaultAudioEndpoint(int dataFlow, int role, out IMMDevice? device);
}

[GeneratedComInterface]
[Guid("d666063f-1587-4e43-81f1-b948e807363f")]
internal partial interface IMMDevice
{
    [PreserveSig]
    int Activate(in Guid interfaceId, uint context, nint activationParams, out IAudioEndpointVolume? volume);
}

/// <summary>IAudioEndpointVolume up to GetMute. BOOLs are ints.</summary>
[GeneratedComInterface]
[Guid("5cdf2c82-841e-4546-9722-0cf74078229a")]
internal partial interface IAudioEndpointVolume
{
    [PreserveSig]
    int RegisterControlChangeNotify(IAudioEndpointVolumeCallback callback);

    [PreserveSig]
    int UnregisterControlChangeNotify(IAudioEndpointVolumeCallback callback);

    [PreserveSig]
    int GetChannelCount(out uint count);

    [PreserveSig]
    int SetMasterVolumeLevel(float decibels, in Guid context);

    [PreserveSig]
    int SetMasterVolumeLevelScalar(float level, in Guid context);

    [PreserveSig]
    int GetMasterVolumeLevel(out float decibels);

    [PreserveSig]
    int GetMasterVolumeLevelScalar(out float level);

    [PreserveSig]
    int SetChannelVolumeLevel(uint channel, float decibels, in Guid context);

    [PreserveSig]
    int SetChannelVolumeLevelScalar(uint channel, float level, in Guid context);

    [PreserveSig]
    int GetChannelVolumeLevel(uint channel, out float decibels);

    [PreserveSig]
    int GetChannelVolumeLevelScalar(uint channel, out float level);

    [PreserveSig]
    int SetMute(int muted, in Guid context);

    [PreserveSig]
    int GetMute(out int muted);
}

/// <summary>IAudioEndpointVolumeCallback: data points at an AUDIO_VOLUME_NOTIFICATION_DATA.</summary>
[GeneratedComInterface]
[Guid("657804fa-d6ad-4496-8a60-352752af4f89")]
internal partial interface IAudioEndpointVolumeCallback
{
    [PreserveSig]
    int OnNotify(nint data);
}

/// <summary>IWbemLocator: connects to a WMI namespace.</summary>
[GeneratedComInterface]
[Guid("dc12a687-737f-11cf-884d-00aa004b2e24")]
internal partial interface IWbemLocator
{
    [PreserveSig]
    int ConnectServer(
        [MarshalAs(UnmanagedType.BStr)] string resource,
        [MarshalAs(UnmanagedType.BStr)] string? user,
        [MarshalAs(UnmanagedType.BStr)] string? password,
        [MarshalAs(UnmanagedType.BStr)] string? locale,
        int securityFlags,
        [MarshalAs(UnmanagedType.BStr)] string? authority,
        nint context,
        out nint services);
}

/// <summary>IWbemServices up to ExecMethod (the async and class/instance editing slots are placeholders).</summary>
[GeneratedComInterface]
[Guid("9556dc99-828c-11cf-a37e-00aa003240c7")]
internal partial interface IWbemServices
{
    [PreserveSig] int OpenNamespace(nint ns, int flags, nint context, nint result, nint callResult);
    [PreserveSig] int CancelAsyncCall(nint sink);
    [PreserveSig] int QueryObjectSink(int flags, nint sink);
    [PreserveSig] int GetObject([MarshalAs(UnmanagedType.BStr)] string path, int flags, nint context, out IWbemClassObject? result, nint callResult);
    [PreserveSig] int GetObjectAsync(nint path, int flags, nint context, nint sink);
    [PreserveSig] int PutClass(nint value, int flags, nint context, nint callResult);
    [PreserveSig] int PutClassAsync(nint value, int flags, nint context, nint sink);
    [PreserveSig] int DeleteClass(nint name, int flags, nint context, nint callResult);
    [PreserveSig] int DeleteClassAsync(nint name, int flags, nint context, nint sink);
    [PreserveSig] int CreateClassEnum(nint superclass, int flags, nint context, nint result);
    [PreserveSig] int CreateClassEnumAsync(nint superclass, int flags, nint context, nint sink);
    [PreserveSig] int PutInstance(nint instance, int flags, nint context, nint callResult);
    [PreserveSig] int PutInstanceAsync(nint instance, int flags, nint context, nint sink);
    [PreserveSig] int DeleteInstance(nint path, int flags, nint context, nint callResult);
    [PreserveSig] int DeleteInstanceAsync(nint path, int flags, nint context, nint sink);
    [PreserveSig] int CreateInstanceEnum(nint className, int flags, nint context, nint result);
    [PreserveSig] int CreateInstanceEnumAsync(nint className, int flags, nint context, nint sink);
    [PreserveSig] int ExecQuery([MarshalAs(UnmanagedType.BStr)] string language, [MarshalAs(UnmanagedType.BStr)] string query, int flags, nint context, out IEnumWbemClassObject? result);
    [PreserveSig] int ExecQueryAsync(nint language, nint query, int flags, nint context, nint sink);
    [PreserveSig] int ExecNotificationQuery(nint language, nint query, int flags, nint context, nint result);
    [PreserveSig] int ExecNotificationQueryAsync(nint language, nint query, int flags, nint context, nint sink);
    [PreserveSig] int ExecMethod([MarshalAs(UnmanagedType.BStr)] string path, [MarshalAs(UnmanagedType.BStr)] string method, int flags, nint context, IWbemClassObject inParams, out IWbemClassObject? outParams, nint callResult);
}

/// <summary>IEnumWbemClassObject: a query's results.</summary>
[GeneratedComInterface]
[Guid("027947e1-d731-11ce-a357-000000000001")]
internal partial interface IEnumWbemClassObject
{
    [PreserveSig] int Reset();
    [PreserveSig] int Next(int timeout, uint count, out IWbemClassObject? item, out uint returned);
}

/// <summary>IWbemClassObject up to GetMethod.</summary>
[GeneratedComInterface(StringMarshalling = StringMarshalling.Utf16)]
[Guid("dc12a681-737f-11cf-884d-00aa004b2e24")]
internal partial interface IWbemClassObject
{
    [PreserveSig] int GetQualifierSet(out nint qualifiers);
    [PreserveSig] int Get(string name, int flags, out Variant value, nint type, nint flavor);
    [PreserveSig] int Put(string name, int flags, in Variant value, int type);
    [PreserveSig] int Delete(nint name);
    [PreserveSig] int GetNames(nint qualifierName, int flags, nint qualifierValue, out nint names);
    [PreserveSig] int BeginEnumeration(int flags);
    [PreserveSig] int Next(int flags, out nint name, nint value, nint type, nint flavor);
    [PreserveSig] int EndEnumeration();
    [PreserveSig] int GetPropertyQualifierSet(nint property, out nint qualifiers);
    [PreserveSig] int Clone(out nint copy);
    [PreserveSig] int GetObjectText(int flags, out nint text);
    [PreserveSig] int SpawnDerivedClass(int flags, out nint newClass);
    [PreserveSig] int SpawnInstance(int flags, out IWbemClassObject? instance);
    [PreserveSig] int CompareTo(int flags, nint other);
    [PreserveSig] int GetPropertyOrigin(nint name, out nint className);
    [PreserveSig] int InheritsFrom(nint ancestor);
    [PreserveSig] int GetMethod(string name, int flags, out IWbemClassObject? inSignature, out IWbemClassObject? outSignature);
}

/// <summary>VARIANT on 64-bit Windows (the only kind Harbor builds for): a type, then the value.</summary>
[StructLayout(LayoutKind.Sequential)]
internal struct Variant
{
    public ushort Type;
    private readonly ushort _reserved1;
    private readonly ushort _reserved2;
    private readonly ushort _reserved3;
    public nint Value;
    private readonly nint _padding;

    public const ushort Int32 = 3;
    public const ushort String = 8;
    public const ushort Byte = 17;

    /// <summary>VT_I4 (how WMI passes a uint32).</summary>
    public static Variant OfInt32(int value) => new() { Type = Int32, Value = value };

    /// <summary>VT_UI1 (a uint8).</summary>
    public static Variant OfByte(byte value) => new() { Type = Byte, Value = value };
}

internal static partial class ControlsNative
{
    public static readonly Guid AudioEndpointVolumeId = new("5cdf2c82-841e-4546-9722-0cf74078229a");
    private static readonly Guid DeviceEnumeratorClass = new("bcde0395-e52f-467c-8e3d-c4579291692e");
    private static readonly Guid DeviceEnumeratorId = new("a95664d2-9614-4f35-a746-de8db63617e6");
    private static readonly Guid WbemLocatorClass = new("4590f811-1d3a-11d0-891f-00aa004b2e24");
    private static readonly Guid WbemLocatorId = new("dc12a687-737f-11cf-884d-00aa004b2e24");
    private const uint InProcServer = 1;

    public static IMMDeviceEnumerator? DeviceEnumerator() =>
        CoCreateInstance(DeviceEnumeratorClass, 0, InProcServer, DeviceEnumeratorId, out IMMDeviceEnumerator? enumerator) >= 0 ? enumerator : null;

    /// <summary>
    /// A connection to a WMI namespace, with the call-level impersonation WMI wants on its proxy, or null. Use it
    /// on one MTA thread pool's threads only: the proxy belongs to that apartment.
    /// </summary>
    public static IWbemServices? ConnectWmi(string ns)
    {
        if (CoCreateInstance(WbemLocatorClass, 0, InProcServer, WbemLocatorId, out IWbemLocator? locator) < 0 || locator is null)
        {
            return null;
        }
        try
        {
            if (locator.ConnectServer(ns, null, null, null, 0, null, 0, out var raw) < 0 || raw == 0)
            {
                return null;
            }
            try
            {
                // RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, EOAC_NONE.
                if (CoSetProxyBlanket(raw, 10, 0, 0, 3, 3, 0, 0) < 0)
                {
                    return null;
                }
                return (IWbemServices)new StrategyBasedComWrappers().GetOrCreateObjectForComInstance(raw, CreateObjectFlags.UniqueInstance);
            }
            finally
            {
                Marshal.Release(raw);
            }
        }
        finally
        {
            Release(locator);
        }
    }

    /// <summary>
    /// A property of a WQL query's first result, or an empty VARIANT if there is none: the caller clears it. On the
    /// connection's apartment (thread-pool threads) only.
    /// </summary>
    public static Variant FirstValue(IWbemServices wmi, string query, string property)
    {
        const int ForwardOnly = 0x20;
        const int ReturnImmediately = 0x10;
        const int NextTimeoutMs = 5000;
        if (wmi.ExecQuery("WQL", query, ForwardOnly | ReturnImmediately, 0, out var results) < 0 || results is null)
        {
            return default;
        }
        try
        {
            if (results.Next(NextTimeoutMs, 1, out var item, out var returned) < 0 || returned != 1 || item is null)
            {
                return default;
            }
            try
            {
                return item.Get(property, 0, out var value, 0, 0) >= 0 ? value : default;
            }
            finally
            {
                Release(item);
            }
        }
        finally
        {
            Release(results);
        }
    }

    /// <summary>Lets go of a COM object now rather than when the GC gets to it.</summary>
    public static void Release(object? item)
    {
        if (item is ComObject com)
        {
            com.FinalRelease();
        }
    }

    /// <summary>
    /// Tells every top-level window the light/dark mode changed (WM_SETTINGCHANGE "ImmersiveColorSet"), skipping
    /// hung ones. Can take a while: never on a UI thread.
    /// </summary>
    public static void BroadcastThemeChange()
    {
        const nint Broadcast = 0xffff;
        const uint SettingChange = 0x001a;
        const uint AbortIfHung = 0x0002;
        _ = SendMessageTimeoutW(Broadcast, SettingChange, 0, "ImmersiveColorSet", AbortIfHung, 1000, out _);
    }

    [LibraryImport("ole32.dll")]
    private static partial int CoCreateInstance(in Guid classId, nint outer, uint context, in Guid interfaceId, out IMMDeviceEnumerator? instance);

    [LibraryImport("ole32.dll")]
    private static partial int CoCreateInstance(in Guid classId, nint outer, uint context, in Guid interfaceId, out IWbemLocator? instance);

    [LibraryImport("ole32.dll")]
    private static partial int CoSetProxyBlanket(nint proxy, uint authentication, uint authorization, nint principal, uint level, uint impersonation, nint identity, uint capabilities);

    [LibraryImport("oleaut32.dll")]
    public static partial int VariantClear(ref Variant value);

    [LibraryImport("user32.dll", StringMarshalling = StringMarshalling.Utf16)]
    private static partial nint SendMessageTimeoutW(nint window, uint message, nint wParam, string lParam, uint flags, uint timeout, out nint result);
}
