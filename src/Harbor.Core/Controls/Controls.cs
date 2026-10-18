using Harbor.Core.Logging;

namespace Harbor.Core.Controls;

/// <summary>A radio the island switches.</summary>
public enum RadioKind
{
    WiFi,
    Bluetooth,
}

/// <summary>What the island links to instead of switching: these have no public API (docs/adr/0016).</summary>
public enum SettingsPage
{
    Focus,
    NightLight,
    AirplaneMode,
    PowerMode,
}

public static class SettingsPages
{
    /// <summary>The Settings app's page for it.</summary>
    public static Uri UriOf(SettingsPage page) => new(page switch
    {
        SettingsPage.Focus => "ms-settings:quiethours",
        SettingsPage.NightLight => "ms-settings:nightlight",
        SettingsPage.AirplaneMode => "ms-settings:network-airplanemode",
        SettingsPage.PowerMode => "ms-settings:powersleep",
        _ => throw new ArgumentOutOfRangeException(nameof(page)),
    });
}

/// <summary>A control the island can show.</summary>
public enum QuickControl
{
    Volume,
    Brightness,
    WiFi,
    Bluetooth,
    Dark,
}

/// <summary>
/// The quick controls as read (docs/adr/0016). Null: not on this PC (no output device, no built-in display, no
/// such radio, the theme unreadable), and the island hides that control.
/// </summary>
/// <param name="Volume">The default output's level, 0-1.</param>
/// <param name="Brightness">The built-in display's brightness, 0-100.</param>
/// <param name="Dark">Apps and Windows both in dark mode.</param>
public sealed record ControlsState(double? Volume, bool Muted, int? Brightness, bool? WiFi, bool? Bluetooth, bool? Dark)
{
    public static ControlsState None { get; } = new(null, false, null, null, null, null);

    /// <summary>The controls to show, in the island's order: only those this PC has.</summary>
    public IReadOnlyList<QuickControl> Shown()
    {
        var shown = new List<QuickControl>(5);
        if (Volume is not null)
        {
            shown.Add(QuickControl.Volume);
        }
        if (Brightness is not null)
        {
            shown.Add(QuickControl.Brightness);
        }
        if (WiFi is not null)
        {
            shown.Add(QuickControl.WiFi);
        }
        if (Bluetooth is not null)
        {
            shown.Add(QuickControl.Bluetooth);
        }
        if (Dark is not null)
        {
            shown.Add(QuickControl.Dark);
        }
        return shown;
    }
}

/// <summary>
/// Port: the island's quick controls (docs/adr/0016). They are read and followed only while <see cref="Watching"/>
/// (the island open); a change is asked for and then shows up in <see cref="Current"/>, so a refused one never
/// shows as done.
/// </summary>
public interface ISystemControls : IDisposable
{
    ControlsState Current { get; }

    /// <summary>Current changed. Raised on any thread.</summary>
    event Action? Changed;

    /// <summary>True while the island shows the controls: read them again and follow them. False lets go.</summary>
    bool Watching { get; set; }

    void SetVolume(double level);

    void SetMuted(bool muted);

    void SetBrightness(int percent);

    void SetRadio(RadioKind radio, bool switchedOn);

    void SetDark(bool dark);

    void OpenSettings(SettingsPage page);
}

/// <summary>Wi-Fi and Bluetooth: on, off, or null when there is no such radio.</summary>
public readonly record struct RadioStates(bool? WiFi, bool? Bluetooth);

/// <summary>Port: the radios (Windows.Devices.Radios, an adapter in Harbor.Platform).</summary>
public interface IRadioSwitches : IDisposable
{
    /// <summary>Their states now; follows them (<see cref="Changed"/>) until <see cref="StopFollowing"/>.</summary>
    Task<RadioStates> StartAsync();

    void StopFollowing();

    /// <summary>A radio was switched (by anyone). Raised on any thread.</summary>
    event Action<RadioStates>? Changed;

    /// <summary>False if Windows refused or there is no such radio.</summary>
    Task<bool> SetAsync(RadioKind radio, bool switchedOn);
}

/// <summary>
/// Test Shells' controls: made-up values that change only here and are logged, so a test sees what the island
/// asked for and the machine's volume, display, radios, theme and Settings app are never touched. Like a desktop
/// PC, it has no brightness.
/// </summary>
public sealed class TestSystemControls(ILog log) : ISystemControls
{
    private readonly Lock _lock = new();
    private ControlsState _current = new(Volume: 0.5, Muted: false, Brightness: null, WiFi: true, Bluetooth: false, Dark: false);

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

    public bool Watching { get; set; }

    public void SetVolume(double level) => Apply($"volume {Math.Round(Math.Clamp(level, 0, 1) * 100):0}", state => state with { Volume = Math.Clamp(level, 0, 1) });

    public void SetMuted(bool muted) => Apply(muted ? "muted" : "unmuted", state => state with { Muted = muted });

    public void SetBrightness(int percent) => Apply($"brightness {percent}", state => state);

    public void SetRadio(RadioKind radio, bool switchedOn) => Apply(
        $"{radio} {(switchedOn ? "on" : "off")}",
        state => radio == RadioKind.WiFi ? state with { WiFi = switchedOn } : state with { Bluetooth = switchedOn });

    public void SetDark(bool dark) => Apply(dark ? "dark" : "light", state => state with { Dark = dark });

    public void OpenSettings(SettingsPage page) => log.Info($"controls: test settings {page}");

    private void Apply(string what, Func<ControlsState, ControlsState> change)
    {
        lock (_lock)
        {
            _current = change(_current);
        }
        log.Info($"controls: test {what}");
        Changed?.Invoke();
    }

    public void Dispose()
    {
    }
}
