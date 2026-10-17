using System.Runtime.InteropServices;
using Harbor.Core.Interop;

namespace Harbor.Core.Controls;

/// <summary>
/// The built-in display's brightness through WMI (root\wmi WmiMonitorBrightness, docs/adr/0016). Desktops and
/// external displays have none: null. The connection is a proxy of the MTA: call it on thread-pool threads only,
/// never a UI thread.
/// </summary>
internal sealed class WmiBrightness : IDisposable
{
    private readonly Lock _lock = new();
    private IWbemServices? _wmi;

    /// <summary>0-100, or null if this PC's display has no brightness WMI can read.</summary>
    public int? Read()
    {
        lock (_lock)
        {
            var value = FirstValue("SELECT CurrentBrightness FROM WmiMonitorBrightness WHERE Active = TRUE", "CurrentBrightness");
            try
            {
                return value.Type == Variant.Byte ? (byte)value.Value : null;
            }
            finally
            {
                _ = ControlsNative.VariantClear(ref value);
            }
        }
    }

    /// <summary>WmiMonitorBrightnessMethods.WmiSetBrightness(0, percent). False if there's no such display.</summary>
    public bool Set(int percent)
    {
        lock (_lock)
        {
            var pathValue = FirstValue("SELECT __PATH FROM WmiMonitorBrightnessMethods WHERE Active = TRUE", "__PATH");
            string? path;
            try
            {
                path = pathValue.Type == Variant.String && pathValue.Value != 0 ? Marshal.PtrToStringBSTR(pathValue.Value) : null;
            }
            finally
            {
                _ = ControlsNative.VariantClear(ref pathValue);
            }
            if (path is null || _wmi is not { } wmi)
            {
                return false;
            }
            if (wmi.GetObject("WmiMonitorBrightnessMethods", 0, 0, out var methods, 0) < 0 || methods is null)
            {
                return false;
            }
            IWbemClassObject? signature = null;
            IWbemClassObject? output = null;
            IWbemClassObject? input = null;
            IWbemClassObject? result = null;
            try
            {
                if (methods.GetMethod("WmiSetBrightness", 0, out signature, out output) < 0 || signature is null ||
                    signature.SpawnInstance(0, out input) < 0 || input is null ||
                    input.Put("Timeout", 0, Variant.OfInt32(0), 0) < 0 ||
                    input.Put("Brightness", 0, Variant.OfByte((byte)Math.Clamp(percent, 0, 100)), 0) < 0)
                {
                    return false;
                }
                return wmi.ExecMethod(path, "WmiSetBrightness", 0, 0, input, out result, 0) >= 0;
            }
            finally
            {
                ControlsNative.Release(result);
                ControlsNative.Release(input);
                ControlsNative.Release(output);
                ControlsNative.Release(signature);
                ControlsNative.Release(methods);
            }
        }
    }

    private Variant FirstValue(string query, string property)
    {
        _wmi ??= ControlsNative.ConnectWmi(@"root\wmi");
        return _wmi is { } wmi ? ControlsNative.FirstValue(wmi, query, property) : default;
    }

    /// <summary>Lets go of the WMI connection (on a thread-pool thread, like every other call).</summary>
    public void Close()
    {
        lock (_lock)
        {
            ControlsNative.Release(_wmi);
            _wmi = null;
        }
    }

    public void Dispose() => Close();
}
