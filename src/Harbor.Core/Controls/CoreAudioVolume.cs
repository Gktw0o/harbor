using System.Runtime.InteropServices;
using System.Runtime.InteropServices.Marshalling;
using Harbor.Core.Interop;

namespace Harbor.Core.Controls;

/// <summary>
/// The default output's volume and mute (Core Audio, docs/adr/0016), followed through its change callback while
/// open. Harbor's own changes carry <see cref="OwnChanges"/>, so they can be told apart. Its objects are
/// free-threaded: any thread may call it.
/// </summary>
internal sealed class CoreAudioVolume : IDisposable
{
    /// <summary>The event context Harbor's changes carry.</summary>
    public static readonly Guid OwnChanges = new("3f0c8a52-6e1d-4f4b-9b7e-2d5a8c41e0b7");

    private const int Render = 0;
    private const int Multimedia = 1;
    private const uint AllContexts = 0x17;
    private readonly Lock _lock = new();
    private IAudioEndpointVolume? _volume;
    private VolumeCallback? _callback;

    /// <summary>
    /// Opens the default output and follows it: changed gets (context, level 0-1, muted) on an audio thread. Null
    /// if there is no output device.
    /// </summary>
    public (double Level, bool Muted)? Open(Action<Guid, double, bool> changed)
    {
        lock (_lock)
        {
            if (_volume is null)
            {
                var enumerator = ControlsNative.DeviceEnumerator();
                if (enumerator is null)
                {
                    return null;
                }
                try
                {
                    if (enumerator.GetDefaultAudioEndpoint(Render, Multimedia, out var device) < 0 || device is null)
                    {
                        return null;
                    }
                    try
                    {
                        if (device.Activate(ControlsNative.AudioEndpointVolumeId, AllContexts, 0, out var volume) < 0 || volume is null)
                        {
                            return null;
                        }
                        _volume = volume;
                    }
                    finally
                    {
                        ControlsNative.Release(device);
                    }
                }
                finally
                {
                    ControlsNative.Release(enumerator);
                }
                var callback = new VolumeCallback(changed);
                _callback = _volume.RegisterControlChangeNotify(callback) >= 0 ? callback : null;
            }
            if (_volume.GetMasterVolumeLevelScalar(out var level) < 0 || _volume.GetMute(out var muted) < 0)
            {
                return null;
            }
            return (Math.Clamp(level, 0, 1), muted != 0);
        }
    }

    public void SetLevel(double level)
    {
        lock (_lock)
        {
            _ = _volume?.SetMasterVolumeLevelScalar((float)Math.Clamp(level, 0, 1), OwnChanges);
        }
    }

    public void SetMuted(bool muted)
    {
        lock (_lock)
        {
            _ = _volume?.SetMute(muted ? 1 : 0, OwnChanges);
        }
    }

    /// <summary>Stops following and lets go of the device (the callback holds no lock this takes).</summary>
    public void Close()
    {
        lock (_lock)
        {
            if (_volume is { } volume)
            {
                if (_callback is { } callback)
                {
                    _ = volume.UnregisterControlChangeNotify(callback);
                }
                ControlsNative.Release(volume);
            }
            _volume = null;
            _callback = null;
        }
    }

    public void Dispose() => Close();
}

/// <summary>Core Audio's volume callback: reads AUDIO_VOLUME_NOTIFICATION_DATA (context, muted at 16, level at 20).</summary>
[GeneratedComClass]
internal sealed partial class VolumeCallback(Action<Guid, double, bool> changed) : IAudioEndpointVolumeCallback
{
    public int OnNotify(nint data)
    {
        if (data != 0)
        {
            var context = Marshal.PtrToStructure<Guid>(data);
            var muted = Marshal.ReadInt32(data, 16) != 0;
            var level = BitConverter.Int32BitsToSingle(Marshal.ReadInt32(data, 20));
            changed(context, Math.Clamp(level, 0, 1), muted);
        }
        return 0;
    }
}
