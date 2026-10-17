using System.Diagnostics;
using System.Runtime.InteropServices;
using Harbor.Core.Logging;

namespace Harbor.Core.Controls;

/// <summary>
/// The island's quick controls on Windows (docs/adr/0016): Core Audio volume, WMI brightness, the radios (an
/// adapter from Harbor.Platform), light/dark mode and the Settings links. Read and followed only while
/// <see cref="Watching"/>; the slow parts (WMI, the theme broadcast) run on thread-pool threads, one at a time.
/// </summary>
public sealed class WindowsSystemControls : ISystemControls
{
    private readonly ILog _log;
    private readonly IRadioSwitches _radios;
    private readonly Lock _lock = new();
    private readonly SemaphoreSlim _work = new(1, 1);
    private readonly CoreAudioVolume _volume = new();
    private readonly WmiBrightness _brightness = new();
    private ControlsState _current = ControlsState.None;
    private string? _available;
    private bool _watching;
    private int? _brightnessWanted;
    private bool _brightnessBusy;

    public WindowsSystemControls(ILog log, IRadioSwitches radios)
    {
        ArgumentNullException.ThrowIfNull(radios);
        _log = log;
        _radios = radios;
        _radios.Changed += states => Update(state => state with { WiFi = states.WiFi, Bluetooth = states.Bluetooth });
    }

    public ControlsState Current
    {
        get
        {
            lock (_lock)
            {
                return _current;
            }
        }
    }

    public event Action? Changed;

    public bool Watching
    {
        get
        {
            lock (_lock)
            {
                return _watching;
            }
        }
        set
        {
            lock (_lock)
            {
                if (value == _watching)
                {
                    return;
                }
                _watching = value;
            }
            // Each change queues a sync; the last one sees the last wish.
            _ = Task.Run(SyncAsync);
        }
    }

    private async Task SyncAsync()
    {
        await _work.WaitAsync().ConfigureAwait(false);
        try
        {
            if (Watching)
            {
                var volume = _volume.Open(OnVolumeChanged);
                var brightness = _brightness.Read();
                var dark = RegistryTheme.ReadDark();
                var radios = await _radios.StartAsync().ConfigureAwait(false);
                var state = new ControlsState(volume?.Level, volume?.Muted ?? false, brightness, radios.WiFi, radios.Bluetooth, dark);
                lock (_lock)
                {
                    _current = state;
                }
                LogAvailable(state);
                Changed?.Invoke();
            }
            else
            {
                _volume.Close();
                _radios.StopFollowing();
                _brightness.Close();
            }
        }
        catch (Exception exception) when (exception is COMException or InvalidOperationException or InvalidCastException)
        {
            _log.Warn($"controls: not read: {exception.GetType().Name} {exception.HResult:X8}");
        }
        finally
        {
            _work.Release();
        }
    }

    // Which controls this PC has, when that changes (never their values).
    private void LogAvailable(ControlsState state)
    {
        var available = string.Join(", ", state.Shown());
        if (available != _available)
        {
            _available = available;
            _log.Info($"controls: available: {(available.Length == 0 ? "none" : available)}");
        }
    }

    private void OnVolumeChanged(Guid context, double level, bool muted)
    {
        // Harbor's own changes are already in Current (the slider is ahead of them while it is dragged).
        if (context != CoreAudioVolume.OwnChanges)
        {
            Update(state => state with { Volume = level, Muted = muted });
        }
    }

    private void Update(Func<ControlsState, ControlsState> change)
    {
        lock (_lock)
        {
            if (!_watching)
            {
                return;
            }
            _current = change(_current);
        }
        Changed?.Invoke();
    }

    public void SetVolume(double level)
    {
        _volume.SetLevel(level);
        lock (_lock)
        {
            _current = _current with { Volume = Math.Clamp(level, 0, 1) };
        }
    }

    public void SetMuted(bool muted)
    {
        _volume.SetMuted(muted);
        Update(state => state with { Muted = muted });
    }

    /// <summary>The latest wish wins: a dragged slider asks often and WMI is slow.</summary>
    public void SetBrightness(int percent)
    {
        lock (_lock)
        {
            _brightnessWanted = Math.Clamp(percent, 0, 100);
            if (_brightnessBusy)
            {
                return;
            }
            _brightnessBusy = true;
        }
        _ = Task.Run(ApplyBrightness);
    }

    private void ApplyBrightness()
    {
        while (true)
        {
            int? wanted;
            lock (_lock)
            {
                wanted = _brightnessWanted;
                _brightnessWanted = null;
            }
            if (wanted is { } percent)
            {
                if (!_brightness.Set(percent))
                {
                    _log.Warn("controls: brightness not set");
                }
                continue;
            }
            var now = _brightness.Read();
            lock (_lock)
            {
                if (_brightnessWanted is not null)
                {
                    continue;
                }
                _brightnessBusy = false;
            }
            Update(state => state with { Brightness = now ?? state.Brightness });
            if (!Watching)
            {
                // Closed while this ran: the connection isn't kept open for nothing.
                _brightness.Close();
            }
            return;
        }
    }

    public void SetRadio(RadioKind radio, bool switchedOn) => _ = SetRadioAsync(radio, switchedOn);

    private async Task SetRadioAsync(RadioKind radio, bool switchedOn)
    {
        if (!await _radios.SetAsync(radio, switchedOn).ConfigureAwait(false))
        {
            // Refused: the island shows the radio as it still is.
            Changed?.Invoke();
        }
    }

    public void SetDark(bool dark) => _ = Task.Run(() =>
    {
        _log.Info(RegistryTheme.SetDark(dark) ? $"controls: {(dark ? "dark" : "light")} mode" : "controls: the mode wasn't changed");
        var now = RegistryTheme.ReadDark();
        Update(state => state with { Dark = now });
    });

    public void OpenSettings(SettingsPage page)
    {
        try
        {
            using var process = Process.Start(new ProcessStartInfo(SettingsPages.UriOf(page).ToString()) { UseShellExecute = true });
            _log.Info($"controls: settings {page}");
        }
        catch (Exception exception) when (exception is System.ComponentModel.Win32Exception or InvalidOperationException)
        {
            _log.Warn($"controls: settings {page} didn't open: {exception.GetType().Name}");
        }
    }

    public void Dispose()
    {
        lock (_lock)
        {
            _watching = false;
        }
        _volume.Dispose();
        _radios.Dispose();
        // The WMI proxy belongs to the thread pool's apartment.
        _ = Task.Run(_brightness.Dispose);
    }
}
