using System.Runtime.InteropServices;
using Harbor.Core.Controls;
using Harbor.Core.Interop;

namespace Harbor.Core.Tests;

[TestClass]
public sealed class ControlsTests
{
    [TestMethod]
    public void TheIslandShowsOnlyTheControlsThisPcHas()
    {
        Assert.AreEqual(0, ControlsState.None.Shown().Count);
        // A desktop PC: no built-in display, no Wi-Fi card.
        var desktop = new ControlsState(Volume: 0.3, Muted: false, Brightness: null, WiFi: null, Bluetooth: true, Dark: false);
        CollectionAssert.AreEqual(new[] { QuickControl.Volume, QuickControl.Bluetooth, QuickControl.Dark }, desktop.Shown().ToArray());
        // Off is not missing: a muted, dark-off, radios-off laptop shows every control.
        var laptop = new ControlsState(Volume: 0, Muted: true, Brightness: 0, WiFi: false, Bluetooth: false, Dark: false);
        CollectionAssert.AreEqual(
            new[] { QuickControl.Volume, QuickControl.Brightness, QuickControl.WiFi, QuickControl.Bluetooth, QuickControl.Dark },
            laptop.Shown().ToArray());
        // No output device and the theme unreadable.
        var server = new ControlsState(Volume: null, Muted: false, Brightness: null, WiFi: true, Bluetooth: null, Dark: null);
        CollectionAssert.AreEqual(new[] { QuickControl.WiFi }, server.Shown().ToArray());
    }

    [TestMethod]
    public void ATestShellNeverGetsTheMachinesControls()
    {
        using var dir = new TempDirectory();
        var (options, _) = ShellOptions.Parse(["--pipe-name", "harbor.test.controls", "--data-dir", dir.Path, "--test-taskbar", "controls"]);
        using var controls = ShellHost.ControlsFor(options!, new CapturingLog(), real: () => throw new AssertFailedException("the machine's controls were made for a test Shell"));
        Assert.IsInstanceOfType<TestSystemControls>(controls);
    }

    [TestMethod]
    public void TestControlsChangeOnlyThemselvesAndSayWhat()
    {
        var log = new CapturingLog();
        using var controls = new TestSystemControls(log);
        var changes = 0;
        controls.Changed += () => changes++;
        // Like a desktop PC: no brightness, so a test sees a hidden control too.
        Assert.IsNull(controls.Current.Brightness);
        Assert.IsNotNull(controls.Current.Volume);

        controls.SetDark(true);
        controls.SetRadio(RadioKind.WiFi, switchedOn: false);
        controls.SetRadio(RadioKind.Bluetooth, switchedOn: true);
        controls.SetVolume(1.5);
        controls.SetMuted(true);
        controls.OpenSettings(SettingsPage.NightLight);

        Assert.AreEqual(new ControlsState(Volume: 1, Muted: true, Brightness: null, WiFi: false, Bluetooth: true, Dark: true), controls.Current);
        Assert.AreEqual(5, changes);
        foreach (var said in new[] { "controls: test dark", "controls: test WiFi off", "controls: test Bluetooth on", "controls: test volume 100", "controls: test muted", "controls: test settings NightLight" })
        {
            Assert.IsTrue(log.Contains(said), said + " not in:\n" + string.Join("\n", log.Lines));
        }
    }

    [TestMethod]
    public void TheModeIsDarkOnlyWhenAppsAndWindowsAreBothDark()
    {
        Assert.IsTrue(RegistryTheme.IsDark(0, 0));
        Assert.IsFalse(RegistryTheme.IsDark(0, 1));
        Assert.IsFalse(RegistryTheme.IsDark(1, 0));
        Assert.IsFalse(RegistryTheme.IsDark(1, 1));
        // Windows' value missing: light, as Windows reads it.
        Assert.IsFalse(RegistryTheme.IsDark(0, null));
        // The apps' value missing or not a DWORD: unknown, and the switch is hidden.
        Assert.IsNull(RegistryTheme.IsDark(null, 0));
        Assert.IsNull(RegistryTheme.IsDark("0", 0));
    }

    [TestMethod]
    public void EachLinkOpensItsOwnSettingsPage()
    {
        var pages = Enum.GetValues<SettingsPage>().Select(SettingsPages.UriOf).ToList();
        Assert.AreEqual(pages.Count, pages.Distinct().Count());
        foreach (var page in pages)
        {
            Assert.AreEqual("ms-settings", page.Scheme, page.ToString());
        }
        Assert.AreEqual(new Uri("ms-settings:nightlight"), SettingsPages.UriOf(SettingsPage.NightLight));
    }

    [TestMethod]
    public async Task WmiAnswersAQueryEveryWindowsCanAnswer()
    {
        // The brightness read is often null (desktops, CI), so its interop (locator, services, query, results,
        // values) is proved here, on a class every Windows has. Read only, on the thread pool as the Shell does.
        var caption = await Task.Run(() =>
        {
            var wmi = ControlsNative.ConnectWmi(@"root\cimv2");
            Assert.IsNotNull(wmi, "no WMI connection");
            try
            {
                var value = ControlsNative.FirstValue(wmi, "SELECT Caption FROM Win32_OperatingSystem", "Caption");
                try
                {
                    return value.Type == Variant.String && value.Value != 0 ? Marshal.PtrToStringBSTR(value.Value) : $"(type {value.Type})";
                }
                finally
                {
                    _ = ControlsNative.VariantClear(ref value);
                }
            }
            finally
            {
                ControlsNative.Release(wmi);
            }
        });
        StringAssert.Contains(caption, "Windows");
    }

    [TestMethod]
    public async Task ReadingTheMachinesControlsChangesNothingAndNeverThrows()
    {
        // Read only: the volume and mute, the built-in display's brightness (WMI, on the thread pool as the Shell
        // does) and the mode. CI's runners may have none of the first two.
        using (var volume = new CoreAudioVolume())
        {
            if (volume.Open((_, _, _) => { }) is { } read)
            {
                Assert.IsTrue(read.Level is >= 0 and <= 1, read.Level.ToString(System.Globalization.CultureInfo.InvariantCulture));
            }
        }
        var brightness = await Task.Run(() =>
        {
            using var wmi = new WmiBrightness();
            return wmi.Read();
        });
        Assert.IsTrue(brightness is null or (>= 0 and <= 100), brightness?.ToString(System.Globalization.CultureInfo.InvariantCulture));
        _ = RegistryTheme.ReadDark();
    }
}
