using System.Runtime.InteropServices;
using Harbor.Core.Controls;
using Harbor.Core.Logging;
using Windows.Devices.Radios;
using HarborRadio = Harbor.Core.Controls.RadioKind;
using WindowsRadio = Windows.Devices.Radios.RadioKind;

namespace Harbor.Platform.Controls;

/// <summary>
/// Wi-Fi and Bluetooth through Windows.Devices.Radios (docs/adr/0016; works unpackaged with no consent prompt,
/// M4 spike). Followed by their StateChanged events only while started.
/// </summary>
public sealed partial class WinRtRadioSwitches(ILog log) : IRadioSwitches
{
    private readonly Lock _lock = new();
    private Radio? _wifi;
    private Radio? _bluetooth;

    public event Action<RadioStates>? Changed;

    public async Task<RadioStates> StartAsync()
    {
        IReadOnlyList<Radio> radios;
        try
        {
            radios = await Radio.GetRadiosAsync();
        }
        catch (Exception exception) when (exception is COMException or UnauthorizedAccessException)
        {
            log.Warn($"controls: radios aren't available ({exception.HResult:X8})");
            return default;
        }
        lock (_lock)
        {
            Unfollow();
            _wifi = radios.FirstOrDefault(radio => radio.Kind == WindowsRadio.WiFi);
            _bluetooth = radios.FirstOrDefault(radio => radio.Kind == WindowsRadio.Bluetooth);
            foreach (var radio in new[] { _wifi, _bluetooth })
            {
                if (radio is not null)
                {
                    radio.StateChanged += OnStateChanged;
                }
            }
            return Read();
        }
    }

    private void OnStateChanged(Radio sender, object args)
    {
        RadioStates states;
        lock (_lock)
        {
            if (!ReferenceEquals(sender, _wifi) && !ReferenceEquals(sender, _bluetooth))
            {
                return;
            }
            states = Read();
        }
        Changed?.Invoke(states);
    }

    private RadioStates Read() => new(StateOf(_wifi), StateOf(_bluetooth));

    // Disabled: off by a hardware switch or the firmware; Harbor can't turn it on, but it is off.
    private static bool? StateOf(Radio? radio) => radio?.State switch
    {
        RadioState.On => true,
        RadioState.Off or RadioState.Disabled => false,
        _ => null,
    };

    public void StopFollowing()
    {
        lock (_lock)
        {
            Unfollow();
        }
    }

    private void Unfollow()
    {
        foreach (var radio in new[] { _wifi, _bluetooth })
        {
            if (radio is not null)
            {
                radio.StateChanged -= OnStateChanged;
            }
        }
        _wifi = null;
        _bluetooth = null;
    }

    public async Task<bool> SetAsync(HarborRadio radio, bool switchedOn)
    {
        Radio? target;
        lock (_lock)
        {
            target = radio == HarborRadio.WiFi ? _wifi : _bluetooth;
        }
        if (target is null)
        {
            return false;
        }
        try
        {
            var status = await target.SetStateAsync(switchedOn ? RadioState.On : RadioState.Off);
            if (status != RadioAccessStatus.Allowed)
            {
                log.Warn($"controls: {radio} not switched ({status})");
            }
            return status == RadioAccessStatus.Allowed;
        }
        catch (COMException exception)
        {
            log.Warn($"controls: {radio} not switched ({exception.HResult:X8})");
            return false;
        }
    }

    public void Dispose() => StopFollowing();
}
